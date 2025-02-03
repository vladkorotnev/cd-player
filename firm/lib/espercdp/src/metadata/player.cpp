#include <esper-cdp/player.h>

namespace CD {
    static void pollTask(void* pvParameter) {
        Player* player = static_cast<Player*>(pvParameter);
        while(true) {
            player->poll_state();
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    void Player::setup_tasks() {
        vSemaphoreCreateBinary(_cmdSemaphore);
        xSemaphoreGive(_cmdSemaphore);

        xTaskCreate(
            pollTask,
            "CDPoll",
            8000,
            this,
            2,
            &_pollTask
        );
    }

    void Player::poll_state() {
        xSemaphoreTake(_cmdSemaphore, portMAX_DELAY);

        if(sts == State::INIT) {
            cdrom->reset();
            cdrom->wait_ready();
            sts = State::LOAD;
        }
        else {
            const ATAPI::MediaTypeCode media_type = cdrom->check_media();
            const ATAPI::MechInfo* mech = cdrom->query_state();
            const ATAPI::AudioStatus* audio = cdrom->query_position();
            // Initial memory allocation for the slot statuses, even if it's just one
            if(mech->slot_count > slots.size()) {
                for(int i = slots.size(); i < mech->slot_count; i++) {
                    slots.push_back(Slot {
                        .disc_present = mech->changer_slots[i].disc_in,
                        .active = (mech->current_disc == i),
                        .disc = Album(),
                    });
                }
            }

            // Get all the stuff that is more or less deterministic

            if(mech->slot_count > 1) {
                // Update changer slot statuses
                for(int i = 0; i < slots.size(); i++) {
                    slots[i].active = mech->current_disc == i;
                    if(slots[i].active) cur_slot = i; // why? because who knows what's actually in `mech->current_disc`
                    slots[i].disc_present = mech->changer_slots[i].disc_in;
                }
            } 
            else {
                // Simulate a single slot
                slots[0].active = true;
                slots[0].disc_present = (media_type != ATAPI::MediaTypeCode::MTC_DOOR_OPEN);
                cur_slot = 0;
            }

            if(media_type == ATAPI::MediaTypeCode::MTC_DOOR_OPEN) {
                slots[cur_slot].disc = Album();
                sts = State::OPEN;
            }

            // Now here comes The State Machine

            switch(sts) {
                case State::CLOSE:
                    // Closing the tray
                    cdrom->wait_ready();
                    if(media_type != ATAPI::MediaTypeCode::MTC_DOOR_CLOSED_UNKNOWN && media_type != ATAPI::MediaTypeCode::MTC_DOOR_CLOSED_UNKNOWN_ALT) {
                        sts = State::LOAD;
                    }
                break;

                case State::LOAD:
                    // Load state: either just inited, or just closed the tray / changed CDs
                    if(ATAPI::MediaTypeCodeIsAudioCD(media_type)) {
                        // We got an audio CD
                        auto toc = cdrom->read_toc();
                        if(toc.tracks.empty()) {
                            // The CD is not really useful as we cannot seem to read or play it
                            sts = State::BAD_DISC;
                            slots[cur_slot].disc = Album();
                            want_auto_play = false;
                        } else {
                            slots[cur_slot].disc = Album(toc);
                            // TODO: kick off the meta thread somewhere in here... how to ensure this is not deallocated by the time the thread completes?
                            cur_track.track = 1;
                            cur_track.index = 1;
                            if(want_auto_play) {
                                want_auto_play = false;
                                sts = State::PLAY;
                                cdrom->play(slots[cur_slot].disc.tracks.front().disc_position.position, slots[cur_slot].disc.duration);
                            } else {
                                sts = State::STOP;
                            }
                        }
                    } else {
                        // No disc or unreadable disc
                        sts = (media_type == ATAPI::MediaTypeCode::MTC_NO_DISC ? State::NO_DISC : State::BAD_DISC);
                        slots[cur_slot].disc = Album();
                        want_auto_play = false;
                    }
                break;

                case State::CHANGE_DISC:
                    cdrom->wait_ready();
                    if(next_expected_slot == mech->current_disc) {
                        sts = State::LOAD;
                    }
                    break;

                case State::NO_DISC: // fallthrough
                case State::BAD_DISC:
                    // nothing to do
                    break;

                case State::STOP:
                    break;

                case State::PAUSE:
                case State::PLAY:
                case State::SEEK_FF:
                case State::SEEK_REW:
                    cur_track.track = audio->track;
                    cur_track.index = audio->index;
                    abs_ts = audio->position_in_disc;
                    rel_ts = audio->position_in_track;
                    break;
            }
        }

        xSemaphoreGive(_cmdSemaphore);
    }

    void Player::do_command(Command cmd) {
        xSemaphoreTake(_cmdSemaphore, portMAX_DELAY);

        if(cmd == Command::PLAY_PAUSE) {
            if(sts == State::PLAY) cmd = Command::PAUSE;
            else cmd = Command::PLAY;
        }

        if(sts != State::INIT && sts != State::CHANGE_DISC && sts != State::LOAD) {
            // Open/Close works in any state
            if(cmd == Command::OPEN_CLOSE) {
                if(sts == State::OPEN) {
                    cdrom->eject(false);
                    sts = State::CLOSE;
                } else {
                    cdrom->eject(true);
                    sts = State::OPEN;
                }
            }
        }

        switch(sts) {
            case State::INIT:
            case State::LOAD:
                // all controls are inert until device ready
            break;

            case State::NO_DISC:
            case State::BAD_DISC:
                // can't do shit other than eject it really
            break;

            case State::OPEN:
                switch(cmd) {
                    // PLAY while open is like pressing eject to close, then play to play
                    case Command::PLAY:
                        want_auto_play = true;
                        cdrom->eject(false);
                        sts = State::CLOSE;
                    break;

                    default:
                        // any others?
                        break;
                }
            break;

            case State::CLOSE:
                // can't really do shit while closing
            break;

            case State::CHANGE_DISC:
                // ???
            break;

            case State::STOP:
                switch (cmd)
                {
                    case Command::PLAY:
                        {
                            auto album = slots[cur_slot].disc;
                            if(!album.tracks.empty()) {
                                int desired_track_idx = (album.tracks.size() >= cur_track.track ? (cur_track.track - 1) : 0);
                                cdrom->play(album.tracks[desired_track_idx].disc_position.position, album.duration);
                                sts = State::PLAY;
                            }
                        }
                    break;

                    case Command::NEXT_TRACK:
                        {
                            auto album = slots[cur_slot].disc;
                            if(!album.tracks.empty()) {
                                cur_track.track = std::min((int)album.tracks.size(), cur_track.track + 1);
                            }
                        }
                    break;

                    case Command::PREV_TRACK:
                        {
                            auto album = slots[cur_slot].disc;
                            if(!album.tracks.empty()) {
                                cur_track.track = std::max(1, cur_track.track - 1);
                            }
                        }
                    break;
                    
                    default:
                    break;
                }
            break;

            case State::PLAY:
                switch(cmd) {
                    case Command::PAUSE:
                        sts = State::PAUSE;
                        cdrom->pause(true);
                    break;

                    case Command::SEEK_FF:
                        // TODO
                    break;

                    case Command::SEEK_REW:
                        // TODO
                    break;

                    case Command::STOP:
                        sts = State::STOP;
                        cdrom->stop();
                    break;

                    case Command::NEXT_TRACK:
                        {
                            auto album = slots[cur_slot].disc;
                            if(!album.tracks.empty()) {
                                cur_track.track = std::min((int)album.tracks.size(), cur_track.track + 1);
                                int desired_track_idx = (album.tracks.size() >= cur_track.track ? (cur_track.track - 1) : 0);
                                cdrom->play(album.tracks[desired_track_idx].disc_position.position, album.duration);
                            }
                        }
                    break;

                    case Command::PREV_TRACK:
                        {
                            auto album = slots[cur_slot].disc;
                            if(!album.tracks.empty()) {
                                if(rel_ts.M == 0 && rel_ts.S < 3) {
                                    cur_track.track = std::max(1, cur_track.track - 1);
                                }
                                int desired_track_idx = (album.tracks.size() >= cur_track.track ? (cur_track.track - 1) : 0);
                                cdrom->play(album.tracks[desired_track_idx].disc_position.position, album.duration);
                            }
                        }
                    break;

                    case Command::NEXT_DISC:
                        // TODO
                    break;

                    case Command::PREV_DISC:
                        // TODO
                    break;

                    default:
                    break;
                }
            break;
        }

        xSemaphoreGive(_cmdSemaphore);
    }
}