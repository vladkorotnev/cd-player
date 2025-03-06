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
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    void Player::setup_tasks() {
        _cmdSemaphore = xSemaphoreCreateBinary();
        xSemaphoreGive(_cmdSemaphore);

        _metaSemaphore = xSemaphoreCreateBinary();
        xSemaphoreGive(_metaSemaphore);

        xTaskCreate(
            pollTask,
            "CDPoll",
            8000,
            this,
            5,
            &_pollTask
        );

        xTaskCreate(
            metaTask,
            "CDMeta",
            16000,
            this,
            1,
            &_metaTask
        );
    }

    void Player::teardown_tasks() {
        if(_pollTask != NULL) {
            ESP_LOGI(LOG_TAG, "Deleting poll task");
            xSemaphoreTake(_cmdSemaphore, portMAX_DELAY);
            vTaskDelete(_pollTask);
            _pollTask = NULL;
        }
        if(_metaTask != NULL) {
            ESP_LOGI(LOG_TAG, "Deleting metadata task");
            xSemaphoreTake(_metaSemaphore, portMAX_DELAY);
            vTaskDelete(_metaTask);
            _metaTask = NULL;
        }
        if(_metaSemaphore != NULL) {
            vSemaphoreDelete(_metaSemaphore);
            _metaSemaphore = NULL;
        }
        if(_cmdSemaphore != NULL) {
            vSemaphoreDelete(_cmdSemaphore);
            _cmdSemaphore = NULL;
        }
    }

    void Player::process_metadata_queue() {
        if(xSemaphoreTake(_metaSemaphore, portMAX_DELAY)) {
            if(!_metaQueue.empty()) {
                std::shared_ptr<Album> album_ptr = _metaQueue.front();

                meta->fetch_album(*album_ptr);
                _metaQueue.pop();
            }
            xSemaphoreGive(_metaSemaphore);
        }
    }

    void Player::poll_state() {
        int delay = 0;
        xSemaphoreTake(_cmdSemaphore, portMAX_DELAY);

        if(sts == State::INIT) {
            cdrom->start();
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

            if((media_type == ATAPI::MediaTypeCode::MTC_DOOR_OPEN || mech->is_door_open) && sts != State::CLOSE) {
                slots[cur_slot].disc = std::make_shared<Album>();
                sts = State::OPEN;
            }

            // Now here comes The State Machine

            switch(sts) {
                case State::INIT:
                    // WTF?
                    break;
                    
                case State::OPEN:
                    if(media_type != ATAPI::MediaTypeCode::MTC_DOOR_OPEN) {
                        sts = State::CLOSE;
                        delay = 2000;
                    }
                break;

                case State::CLOSE:
                    // Closing the tray
                    cur_track.track = 1;
                    cur_track.index = 1;
                    abs_ts = { .M = 0, .S = 0, .F = 0 };
                    rel_ts = { .M = 0, .S = 0, .F = 0 };
                    cdrom->wait_ready();
                    if((media_type != ATAPI::MediaTypeCode::MTC_DOOR_CLOSED_UNKNOWN && media_type != ATAPI::MediaTypeCode::MTC_DOOR_CLOSED_UNKNOWN_ALT) || (cdrom->get_quirks().no_media_codes && media_type == ATAPI::MediaTypeCode::MTC_DOOR_CLOSED_UNKNOWN)) {
                        sts = (media_type != ATAPI::MediaTypeCode::MTC_DOOR_OPEN) ? State::LOAD : State::OPEN;
                    } else {
                        delay = 2000; //<- some drives cannot load while processing other commands
                    }
                break;

                case State::LOAD:
                    // Load state: either just inited, or just closed the tray / changed CDs
                    if(ATAPI::MediaTypeCodeIsAudioCD(media_type) || cdrom->get_quirks().no_media_codes) {
                        // We got an audio CD probably
                        if(cdrom->get_quirks().no_media_codes) vTaskDelay(pdMS_TO_TICKS(2000));
                        auto toc = cdrom->read_toc();
                        if(toc.tracks.empty()) {
                            // The CD is not really useful as we cannot seem to read or play it
                            ESP_LOGE(LOG_TAG, "Empty TOC!");
                            sts = State::BAD_DISC;
                            slots[cur_slot].disc = std::make_shared<Album>();
                            want_auto_play = false;
                        } else {
                            slots[cur_slot].disc = std::make_shared<Album>(toc);
                            
                            // NB: std::queue is not thread safe, can this lead to a problem? realistically we shouldn't have more than one in the pipeline anyway...
                            xSemaphoreTake(_metaSemaphore, portMAX_DELAY);
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
                        slots[cur_slot].disc = std::make_shared<Album>();
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
                    // nothing to do, but if changer, advance to next disc if any
                    if(slots.size() > 1) {
                        change_discs(true);
                    }
                    break;
                case State::BAD_DISC:
                    if(ATAPI::MediaTypeCodeIsAudioCD(media_type)) {
                        // what if the drive changes its mind
                        sts = State::LOAD;
                    }
                    break;

                case State::STOP:
                    {
                        abs_ts = { .M = 0, .S = 0, .F = 0 };
                        rel_ts = { .M = 0, .S = 0, .F = 0 };
                        const ATAPI::AudioStatus* audio = cdrom->query_position();
                        if(audio->state == ATAPI::AudioStatus::PlayState::Playing) {
                            // we are playing for some other reason, maybe front panel button!
                            sts = State::PLAY;
                        }
                    }
                    break;

                case State::PLAY:
                    {
                        const ATAPI::AudioStatus* audio = cdrom->query_position();
                        if(audio->state == ATAPI::AudioStatus::PlayState::Stopped || audio->track == TRK_NUM_LEAD_OUT) {
                            if(play_mode != PlayMode::PLAYMODE_SHUFFLE || !play_next_shuffled_track()) {
                                if(slots.size() == 1 || !change_discs(true)) {
                                    cur_track.track = 1;
                                    cur_track.index = 1;
                                    abs_ts = { .M = 0, .S = 0, .F = 0 };
                                    rel_ts = { .M = 0, .S = 0, .F = 0 };
                                    sts = State::STOP;
                                }
                            }
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
                    {
                        const ATAPI::AudioStatus* audio = cdrom->query_position();
                        cur_track.track = audio->track;
                        cur_track.index = audio->index;
                        abs_ts = audio->position_in_disc;
                        rel_ts = audio->position_in_track;
                    }
                    break;
                case State::SEEK_FF:
                case State::SEEK_REW:
                    {
                        const ATAPI::AudioStatus* audio = cdrom->query_position();
                        cur_track.track = audio->track;
                        cur_track.index = audio->index;
                        abs_ts = audio->position_in_disc;
                        rel_ts = audio->position_in_track;

                        if(cdrom->get_quirks().must_use_softscan) {
                            TickType_t now = xTaskGetTickCount();
                            if(now - last_softscan_tick >= softscan_interval) {
                                if(now - softscan_start >= softscan_interval * 8) {
                                    softscan_start = now;
                                    softscan_hop = softscan_hop + MSF { .M = 0, .S = 5, .F = 0 };
                                }
                                MSF ss = (sts == State::SEEK_FF) ? (abs_ts + softscan_hop) : (abs_ts - softscan_hop);
                                cdrom->play(ss, slots[cur_slot].disc->duration);
                                last_softscan_tick = now;
                            }
                        }
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
                        sts = State::CLOSE;
                        cdrom->eject(false);
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
                            if(play_mode == PlayMode::PLAYMODE_SHUFFLE && cur_track.track == 1) {
                                play_next_shuffled_track();
                                shuffle_history.clear(); // don't keep 1.1 always in history
                            } else {
                                auto album = slots[cur_slot].disc;
                                if(!album->tracks.empty()) {
                                    int desired_track_idx = (album->tracks.size() >= cur_track.track ? (cur_track.track - 1) : 0);
                                    cdrom->play(album->tracks[desired_track_idx].disc_position.position, album->duration);
                                    sts = State::PLAY;
                                }
                            }
                        }
                    break;

                    case Command::NEXT_TRACK:
                        change_tracks(true);
                    break;

                    case Command::PREV_TRACK:
                        change_tracks(false);
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
                        cur_track.track = 1;
                        cur_track.index = 1;
                        sts = State::STOP;
                        cdrom->stop();
                    break;

                    case Command::NEXT_TRACK:
                        change_tracks(true);
                    break;

                    case Command::PREV_TRACK:
                        change_tracks(false);
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
                        cur_track.track = 1;
                        cur_track.index = 1;
                        cdrom->stop();
                    break;

                    case Command::NEXT_TRACK:
                        change_tracks(true);
                    break;

                    case Command::PREV_TRACK:
                        change_tracks(false);
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
                if(cmd == Command::END_SEEK || (cmd == Command::SEEK_FF && sts == State::SEEK_FF) || (cmd == Command::SEEK_REW && sts == State::SEEK_REW)) {
                    if(pre_seek_sts == State::PLAY) {
                        cdrom->play(abs_ts, get_active_slot().disc->duration);
                        sts = State::PLAY;
                    }
                    else {
                        cdrom->pause(true);
                        sts = State::PAUSE;
                    }
                }
                else if(cmd == Command::PLAY) {
                    cdrom->play(abs_ts, get_active_slot().disc->duration);
                    sts = State::PLAY;
                }
                else if(cmd == Command::STOP) {
                    sts = State::STOP;
                    cur_track.track = 1;
                    cur_track.index = 1;
                    cdrom->stop();
                }
                else if(cmd == Command::SEEK_FF) {
                    sts = pre_seek_sts;
                    start_seeking(true);
                }
                else if(cmd == Command::SEEK_REW) {
                    sts = pre_seek_sts;
                    start_seeking(false);
                }
            break;
        }

        xSemaphoreGive(_cmdSemaphore);
    }

    void Player::change_tracks(bool fwd) {
        auto album = slots[cur_slot].disc;
        if(!album->tracks.empty()) {
            int next_trk_no;
            if(fwd) {
                if(play_mode == PlayMode::PLAYMODE_SHUFFLE && sts != Player::State::STOP) {
                    play_next_shuffled_track();
                } else {
                    next_trk_no = std::min((int) album->tracks.size(), (int) cur_track.track + 1);
                }
            }
            else {
                if(sts == State::STOP || (rel_ts.M == 0 && rel_ts.S <= 3)) {
                    // if stopped or within the first 3 seconds of a song
                    // go back one track
                    if(play_mode == PlayMode::PLAYMODE_SHUFFLE && sts != State::STOP) {
                        if(shuffle_history.empty()) return;
                        next_trk_no = *shuffle_history.rbegin();
                        shuffle_history.erase(next_trk_no);
                    } else {
                        next_trk_no = ((cur_track.track >= 2) ? (cur_track.track - 1) : 1); 
                    }
                } else {
                    next_trk_no = cur_track.track; // go to start of current track
                }
            }
            if(next_trk_no < 1) return;

            if(sts != State::STOP) {
                cdrom->play(album->tracks[next_trk_no - 1].disc_position.position, album->duration);
                if(sts == State::PAUSE) {
                    cdrom->pause(true);
                }
            }
            else {
                cur_track.track = next_trk_no;
            }
        }
    }

    void Player::start_seeking(bool forward) {
        pre_seek_sts = sts;
        if(!cdrom->get_quirks().must_use_softscan) {
            sts = forward ? State::SEEK_FF : State::SEEK_REW;
            cdrom->scan(forward, abs_ts);
        } else {
            softscan_start = xTaskGetTickCount();
            softscan_hop = softscan_hop_default;
            if(sts == State::PAUSE) cdrom->pause(false);
            sts = forward ? State::SEEK_FF : State::SEEK_REW;
        }
    }

    bool Player::change_discs(bool forward) {
        if(slots.size() < 2) return false;

        int disc_count = std::count_if(slots.cbegin(), slots.cend(), [](const Slot &x) { return x.disc_present; });
        if(disc_count == 0) return false;
        
        int i = forward ? ((cur_slot + 1) % slots.size()) : (cur_slot == 0 ? (slots.size()-1) : cur_slot - 1);
        while(!slots[i].disc_present)
            i = forward ? ((i + 1) % slots.size()) : (i == 0 ? (slots.size()-1) : (i - 1));

        if(i == cur_slot) return false;
        
        next_expected_slot = i;

        State old_sts = sts;
        sts = State::CHANGE_DISC;
        if(old_sts == State::PLAY || old_sts == State::PAUSE) {
            cdrom->stop();
        }

        ESP_LOGI(LOG_TAG, "Change slot %i -> %i", cur_slot, next_expected_slot);

        if(old_sts == State::PLAY && slots[next_expected_slot].disc_present) {
            want_auto_play = true;
        }

        cdrom->load_unload(next_expected_slot);
        return true;
    }

    void Player::set_play_mode(PlayMode new_mode) {
        if(new_mode == play_mode) return;
        if(new_mode == PlayMode::PLAYMODE_SHUFFLE) {
            shuffle_history.clear();
        }
        if(sts == State::PLAY || sts == State::PAUSE) {
            if(new_mode == PlayMode::PLAYMODE_CONTINUE && play_mode == PlayMode::PLAYMODE_SHUFFLE) {
                // from shuffle to continue: enqueue the whole disc instead of the active track
                cdrom->play(abs_ts, slots[cur_slot].disc->duration);
                if(sts == State::PAUSE) cdrom->pause(true);
                shuffle_history.clear();
            }
            else if(new_mode == PlayMode::PLAYMODE_SHUFFLE) {
                // from other to shuffle: reenqueue the current track only to receive EOP events properly
                cdrom->play(abs_ts, (cur_track.track == get_active_slot().disc->tracks.size()) ? get_active_slot().disc->duration : get_active_slot().disc->tracks[cur_track.track - 1].disc_position.position);
                if(sts == State::PAUSE) cdrom->pause(true);
            }
        }
        play_mode = new_mode;
    }

    bool Player::play_next_shuffled_track() {
        shuffle_history.insert(cur_track.track);

        auto const tracklist = get_active_slot().disc->tracks;
        int playable_track_count = std::count_if(tracklist.cbegin(), tracklist.cend(), [this](const CD::Track& t) {
            return shuffle_history.find(t.disc_position.number) == shuffle_history.end();
        });
        if(playable_track_count == 0) return false;
        int trk_idx = esp_random() % tracklist.size();
        while(shuffle_history.find(tracklist[trk_idx].disc_position.number) != shuffle_history.end()) {
            trk_idx = esp_random() % tracklist.size();
        }
       
        cdrom->play(tracklist[trk_idx].disc_position.position, (trk_idx == (tracklist.size() - 1)) ? get_active_slot().disc->duration : tracklist[trk_idx + 1].disc_position.position);
        sts = State::PLAY;
        return true;
    }

    void Player::power_down() {
        cdrom->start(false);
    }
}