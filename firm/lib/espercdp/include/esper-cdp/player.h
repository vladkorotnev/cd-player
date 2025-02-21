#pragma once
#include <esper-cdp/atapi.h>
#include <esper-cdp/metadata.h>
#include <memory>
#include <queue>
#include <set>

/// Defines the CD Player model as seen to the end user.
/// You'd normally call this a "view model" but is it really in this context?

namespace CD {
    class Player {
    public:
        enum class State {
            INIT,
            LOAD,
            NO_DISC,
            BAD_DISC,
            OPEN,
            CLOSE,
            CHANGE_DISC,
            STOP,
            PLAY,
            PAUSE,
            SEEK_FF,
            SEEK_REW
        };

        static const char * PlayerStateString(State sts) {
            switch(sts) {
                case State::INIT: return "Initializing";
                case State::LOAD: return "Loading";
                case State::NO_DISC: return "No Disc";
                case State::BAD_DISC: return "Bad Disc";
                case State::OPEN: return "Open";
                case State::CLOSE: return "Close";
                case State::CHANGE_DISC: return "Change Disc";
                case State::STOP: return "Stop";
                case State::PLAY: return "Play";
                case State::PAUSE: return "Pause";
                case State::SEEK_FF: return "FFwd";
                case State::SEEK_REW: return "Rew"; 
                default:
                    {
                        static char tmp[8] = { 0 };
                        snprintf(tmp, 8, "Unknown (%i)", sts);
                        return tmp;
                    }
                    break;
            }
        }

        enum class Command {
            OPEN_CLOSE,
            OPEN,
            CLOSE,
            PLAY,
            PAUSE,
            SEEK_FF,
            SEEK_REW,
            END_SEEK,
            STOP,
            NEXT_TRACK,
            PREV_TRACK,
            NEXT_DISC,
            PREV_DISC
        };

        enum PlayMode {
            /// @brief Play all tracks and discs in sequence
            PLAYMODE_CONTINUE,
            /// @brief Shuffle all tracks on the disc
            PLAYMODE_SHUFFLE,
        };

        struct Slot {
            bool disc_present;
            bool active;
            std::shared_ptr<Album> disc;
        };

        struct TrackNo {
            uint8_t track;
            uint8_t index;
        };

        Player(ATAPI::Device * device, MetadataProvider * meta_provider):
            cdrom(device),
            meta(meta_provider)
        {
            setup_tasks();
        }

        ~Player() {
            teardown_tasks();
        }

        State get_status() { return sts; }
        /// @brief Returns the changer slots statuses, even if the device is not a changer — in such case it will be just one slot. 
        const std::vector<Slot>& get_slots() { return slots; }
        const Slot& get_active_slot() { return slots[cur_slot]; }
        int get_active_slot_index() { return cur_slot; }
        const MSF get_current_absolute_time() { return abs_ts; }
        const MSF get_current_track_time() { return rel_ts; }
        const TrackNo get_current_track_number() { return cur_track; }
        bool is_processing_metadata() { return !_metaQueue.empty(); }
        const PlayMode get_play_mode() { return play_mode; }
        void set_play_mode(PlayMode mode);

        void do_command(Command);

        void poll_state();

        void process_metadata_queue();
    private:
        ATAPI::Device * cdrom;
        MetadataProvider * meta;

        TaskHandle_t _pollTask;
        SemaphoreHandle_t _cmdSemaphore;

        TaskHandle_t _metaTask;
        SemaphoreHandle_t _metaSemaphore;
        std::queue<std::shared_ptr<Album>> _metaQueue;

        TickType_t softscan_start = 0;
        TickType_t last_softscan_tick = 0;
        TickType_t softscan_interval = pdMS_TO_TICKS(250);
        MSF softscan_hop = { .M = 0, .S = 10, .F = 0 };
        const MSF softscan_hop_default = { .M = 0, .S = 10, .F = 0 };

        MSF abs_ts = { .M = 0, .S = 0, .F = 0 };
        MSF rel_ts = { .M = 0, .S = 0, .F = 0 };
        TrackNo cur_track = { .track = 1, .index = 1 };
        State sts = State::INIT;
        State pre_seek_sts = State::INIT;
        std::vector<Slot> slots = {
            // At least one must exist
            Slot { 
                .disc_present = false,
                .active = true,
                .disc = std::make_shared<Album>(Album())
            }
        };
        int next_expected_slot = 0;
        int cur_slot = 0;
        bool want_auto_play = false;

        std::set<int> shuffle_history = {};
        PlayMode play_mode = PlayMode::PLAYMODE_CONTINUE;

        void setup_tasks();
        void teardown_tasks();
        void start_seeking(bool ffwd);
        bool change_discs(bool forward);
        void change_tracks(bool ffwd);
        bool play_next_shuffled_track();
    };
}