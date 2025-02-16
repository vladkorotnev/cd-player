#include <esper-cdp/player.h>
const uint8_t TRK_NUM_LEAD_OUT = 0xAA;
static char LOG_TAG[] = "CDP";

namespace CD {
    static void pollTask(void* pvParameter) {
        Player* player = static_cast<Player*>(pvParameter);
        while(true) {
            player->poll_state();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    static void metaTask(void* pvParameter) {
        Player* player = static_cast<Player*>(pvParameter);
        while(true) {
            player->process_metadata_queue();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    void Player::setup_tasks() {
        _cmdSemaphore = xSemaphoreCreateBinary();
        xSemaphoreGive(_cmdSemaphore);

        _metaSemaphore = xSemaphoreCreateCounting(10, 0);

        xTaskCreate(
            pollTask,
            "CDPoll",
            8000,
            this,
            2,
            &_pollTask
        );

        xTaskCreate(
            metaTask,
            "CDMeta",
            16000,
            this,
            2,
            &_metaTask
        );
    }

    void Player::teardown_tasks() {
        vTaskDelete(_pollTask);
        vTaskDelete(_metaTask);
    }

    void Player::process_metadata_queue() {
        if(xSemaphoreTake(_metaSemaphore, portMAX_DELAY)) {
            std::shared_ptr<Album> album_ptr = _metaQueue.front();
            _metaQueue.pop();

            meta->fetch_album(*album_ptr);
        }
    }

    void Player::poll_state() {
        int delay = 0;
        xSemaphoreTake(_cmdSemaphore, portMAX_DELAY);

        if(sts == State::INIT) {
            cdrom->reset();
            cdrom->wait_ready();
            vTaskDelay(pdMS_TO_TICKS(500));
            cdrom->eject(false); // <- close aka start unit, otherwise any disc is BAD_DISC on some drives
            sts = State::LOAD;
        }
        else {
            const ATAPI::MediaTypeCode media_type = cdrom->check_media();
            const ATAPI::MechInfo* mech = cdrom->query_state();
            // Initial memory allocation for the slot statuses, even if it's just one
            if(mech->slot_count > slots.size()) {
                for(int i = slots.size(); i < mech->slot_count; i++) {
                    slots.push_back(Slot {
                        .disc_present = mech->changer_slots[i].disc_in,
                        .active = (mech->current_disc == i),
                        .disc = std::make_shared<Album>(Album()),
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
                slots[cur_slot].disc = std::make_shared<Album>(Album());
                sts = State::OPEN;
            }

            // Now here comes The State Machine

            switch(sts) {
                case State::OPEN:
                    if(media_type != ATAPI::MediaTypeCode::MTC_DOOR_OPEN) {
                        sts = State::CLOSE;
                        delay = 2000;
                    }
                break;

                case State::CLOSE:
                    // Closing the tray
                    cdrom->wait_ready();
                    if(media_type != ATAPI::MediaTypeCode::MTC_DOOR_CLOSED_UNKNOWN && media_type != ATAPI::MediaTypeCode::MTC_DOOR_CLOSED_UNKNOWN_ALT) {
                        sts = State::LOAD;
                    } else {
                        delay = 2000; //<- some drives cannot load while processing other commands
                    }
                break;

                case State::LOAD:
                    // Load state: either just inited, or just closed the tray / changed CDs
                    if(ATAPI::MediaTypeCodeIsAudioCD(media_type)) {
                        // We got an audio CD
                        auto toc = cdrom->read_toc();
                        if(toc.tracks.empty()) {
                            // The CD is not really useful as we cannot seem to read or play it
                            ESP_LOGE(LOG_TAG, "Empty TOC!");
                            sts = State::BAD_DISC;
                            slots[cur_slot].disc = std::make_shared<Album>(Album());
                            want_auto_play = false;
                        } else {
                            slots[cur_slot].disc = std::make_shared<Album>(Album(toc));
                            
                            // NB: std::queue is not thread safe, can this lead to a problem? realistically we shouldn't have more than one in the pipeline anyway...
                            _metaQueue.push(slots[cur_slot].disc);
                            xSemaphoreGive(_metaSemaphore); // let the background task go

                            cur_track.track = 1;
                            cur_track.index = 1;
                            if(want_auto_play) {
                                want_auto_play = false;
                                sts = State::PLAY;
                                cdrom->play(slots[cur_slot].disc->tracks.front().disc_position.position, slots[cur_slot].disc->duration);
                            } else {
                                sts = State::STOP;
                            }
                        }
                    } else {
                        // No disc or unreadable disc
                        sts = (media_type == ATAPI::MediaTypeCode::MTC_NO_DISC ? State::NO_DISC : State::BAD_DISC);
                        if(sts == State::BAD_DISC) {
                            ESP_LOGE(LOG_TAG, "Bad media code %i", media_type);
                        }
                        slots[cur_slot].disc = std::make_shared<Album>(Album());
                        want_auto_play = false;
                    }
                break;

                case State::CHANGE_DISC:
                    cdrom->wait_ready();
                    if(next_expected_slot == mech->current_disc) {
                        sts = State::LOAD;
                    }
                    delay = 1000;
                    break;

                case State::NO_DISC:
                    // nothing to do
                    break;
                case State::BAD_DISC:
                    if(ATAPI::MediaTypeCodeIsAudioCD(media_type)) {
                        // what if the drive changes its mind
                        sts = State::LOAD;
                    }
                    break;

                case State::STOP:
                    break;

                case State::PLAY:
                    {
                        const ATAPI::AudioStatus* audio = cdrom->query_position();
                        if(audio->state == ATAPI::AudioStatus::PlayState::Stopped || audio->track == TRK_NUM_LEAD_OUT) {
                            sts = State::STOP;
                            cur_track.track = 1;
                            cur_track.index = 1;
                            abs_ts = { .M = 0, .S = 0, .F = 0 };
                            rel_ts = { .M = 0, .S = 0, .F = 0 };
                        }
                        else if(audio->state == ATAPI::AudioStatus::PlayState::Paused) {
                            sts = State::PAUSE;
                        }  
                        else {
                            cur_track.track = audio->track;
                            cur_track.index = audio->index;
                            abs_ts = audio->position_in_disc;
                            rel_ts = audio->position_in_track;
                        }
                    }
                    
                break;

                case State::PAUSE:
                case State::SEEK_FF:
                case State::SEEK_REW:
                    {
                        const ATAPI::AudioStatus* audio = cdrom->query_position();
                        cur_track.track = audio->track;
                        cur_track.index = audio->index;
                        abs_ts = audio->position_in_disc;
                        rel_ts = audio->position_in_track;
                    }
                break;
            }
        }

        xSemaphoreGive(_cmdSemaphore);

        if(delay > 0)
            vTaskDelay(pdMS_TO_TICKS(delay)); //<- some drives go nuts due to other commands during load so give them some time
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
                    cmd = Command::CLOSE;
                } else {
                    cmd = Command::OPEN;
                }
            }

            if(cmd == Command::CLOSE) {
                sts = State::CLOSE;
                cdrom->eject(false);
            } 
            else if(cmd == Command::OPEN) {
                sts = State::OPEN;
                cdrom->eject(true);
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
                if(cmd == Command::NEXT_DISC) {
                    change_discs(true);
                }
                else if(cmd == Command::PREV_DISC) {
                    change_discs(false);
                }
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
                            if(!album->tracks.empty()) {
                                int desired_track_idx = (album->tracks.size() >= cur_track.track ? (cur_track.track - 1) : 0);
                                cdrom->play(album->tracks[desired_track_idx].disc_position.position, album->duration);
                                sts = State::PLAY;
                            }
                        }
                    break;

                    case Command::NEXT_TRACK:
                        {
                            auto album = slots[cur_slot].disc;
                            if(!album->tracks.empty()) {
                                cur_track.track = std::min((int)album->tracks.size(), cur_track.track + 1);
                            }
                        }
                    break;

                    case Command::PREV_TRACK:
                        {
                            auto album = slots[cur_slot].disc;
                            if(!album->tracks.empty()) {
                                cur_track.track = std::max(1, cur_track.track - 1);
                            }
                        }
                    break;
                    
                    case Command::NEXT_DISC:
                        change_discs(true);
                    break;

                    case Command::PREV_DISC:
                        change_discs(false);
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
                        start_seeking(true);
                    break;

                    case Command::SEEK_REW:
                        start_seeking(false);
                    break;

                    case Command::STOP:
                        sts = State::STOP;
                        cdrom->stop();
                    break;

                    case Command::NEXT_TRACK:
                        {
                            auto album = slots[cur_slot].disc;
                            if(!album->tracks.empty()) {
                                if(cur_track.track < album->tracks.size()) {
                                    cur_track.track = std::min((int)album->tracks.size(), cur_track.track + 1);
                                    int desired_track_idx = (album->tracks.size() >= cur_track.track ? (cur_track.track - 1) : 0);
                                    cdrom->play(album->tracks[desired_track_idx].disc_position.position, album->duration);
                                }
                            }
                        }
                    break;

                    case Command::PREV_TRACK:
                        {
                            auto album = slots[cur_slot].disc;
                            if(!album->tracks.empty()) {
                                if(rel_ts.M == 0 && rel_ts.S < 3) {
                                    cur_track.track = std::max(1, cur_track.track - 1);
                                }
                                int desired_track_idx = (album->tracks.size() >= cur_track.track ? (cur_track.track - 1) : 0);
                                cdrom->play(album->tracks[desired_track_idx].disc_position.position, album->duration);
                            }
                        }
                    break;

                    case Command::NEXT_DISC:
                        change_discs(true);
                    break;

                    case Command::PREV_DISC:
                        change_discs(false);
                    break;

                    default:
                    break;
                }
            break;

            case State::PAUSE:
                switch(cmd) {
                    case Command::PLAY:
                    case Command::PAUSE:
                        sts = State::PLAY;
                        cdrom->pause(false);
                    break;

                    case Command::SEEK_FF:
                        start_seeking(true);
                    break;

                    case Command::SEEK_REW:
                        start_seeking(false);
                    break;

                    case Command::STOP:
                        sts = State::STOP;
                        cdrom->stop();
                    break;

                    case Command::NEXT_TRACK:
                        {
                            auto album = slots[cur_slot].disc;
                            if(!album->tracks.empty()) {
                                if(cur_track.track < album->tracks.size()) {
                                    cur_track.track = std::min((int)album->tracks.size(), cur_track.track + 1);
                                    int desired_track_idx = (album->tracks.size() >= cur_track.track ? (cur_track.track - 1) : 0);
                                    cdrom->play(album->tracks[desired_track_idx].disc_position.position, album->duration);
                                    cdrom->pause(true);
                                }
                            }
                        }
                    break;

                    case Command::PREV_TRACK:
                        {
                            auto album = slots[cur_slot].disc;
                            if(!album->tracks.empty()) {
                                if(rel_ts.M == 0 && rel_ts.S < 3) {
                                    cur_track.track = std::max(1, cur_track.track - 1);
                                }
                                int desired_track_idx = (album->tracks.size() >= cur_track.track ? (cur_track.track - 1) : 0);
                                cdrom->play(album->tracks[desired_track_idx].disc_position.position, album->duration);
                                cdrom->pause(true);
                            }
                        }
                    break;

                    case Command::NEXT_DISC:
                        change_discs(true);
                    break;

                    case Command::PREV_DISC:
                        change_discs(false);
                    break;

                    default:
                    break;
                }
            break;

            case State::SEEK_FF:
            case State::SEEK_REW:
                if(cmd == Command::END_SEEK) {
                    if(pre_seek_sts == State::PLAY) {
                        cdrom->play(abs_ts, get_active_slot().disc->duration);
                        sts = State::PLAY;
                    }
                    else {
                        cdrom->pause(true);
                        sts = State::PAUSE;
                    }
                }
            break;
        }

        xSemaphoreGive(_cmdSemaphore);
    }

    void Player::start_seeking(bool forward) {
        pre_seek_sts = sts;
        sts = forward ? State::SEEK_FF : State::SEEK_REW;
        cdrom->scan(forward, abs_ts); //<- seems like not all drives support this (e.g. teac from 2004) -- can we use repeated Play MSF commands instead?
    }

    void Player::change_discs(bool forward) {
        if(sts == State::PLAY) {
            cdrom->stop();
        }

        // TODO: drive might only support selecting slots with discs, what do?
        if(forward) {
            next_expected_slot = (cur_slot + 1) % slots.size();
        }
        else {
            next_expected_slot = (cur_slot == 0 ? (slots.size()-1) : cur_slot - 1);
        }

        ESP_LOGI(LOG_TAG, "Change slot %i -> %i", cur_slot, next_expected_slot);

        if(sts == State::PLAY && slots[next_expected_slot].disc_present) {
            want_auto_play = true;
        }

        cdrom->load_unload(next_expected_slot);
        sts = State::CHANGE_DISC;
    }
}