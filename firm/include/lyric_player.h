#pragma once
#include <esper-cdp/types.h>
#include <esper-cdp/player.h>
#include <esper-cdp/lyrics.h>
#include <esp32-hal-log.h>

struct LyricPlayerState {
    bool must_clear;
    std::string line;
    int length;
};

class LyricPlayer {
public:
    LyricPlayer() {}

    void feed_track(CD::Player::TrackNo trk, const CD::Track& meta) {
        if(trk.track != cur_track.track) {
            cur_metadata = meta;
            cur_track = trk;
            cursor = -1;
        }
    }

    LyricPlayerState feed_position(MSF pos) {
        LyricPlayerState rslt = { 0 };

        if(MSF_TO_FRAMES(pos) < MSF_TO_FRAMES(cur_position)) {
            cursor = -1;
        }

        if (cursor == -1) {
            rslt.must_clear = true;
        }

        cur_position = pos;

        if(!cur_metadata.lyrics.empty()) {
            int cur_ms = MSF_TO_MILLIS(pos);
            int idx = cursor;
            for(int i = std::max(0, idx); i < cur_metadata.lyrics.size(); i++) {
                if(cur_metadata.lyrics[i].millisecond <= cur_ms && cur_metadata.lyrics[i + 1].millisecond > cur_ms) {
                    idx = i;
                    break;
                }
            }
            if(idx != cursor && idx != -1) {
                cursor = idx;
                ESP_LOGI("Lyric", " %i | %i | e=%i\t -= %s =- ", cur_ms, cur_metadata.lyrics[idx].millisecond, (cur_ms - cur_metadata.lyrics[idx].millisecond), cur_metadata.lyrics[idx].line.c_str());
                if(idx < cur_metadata.lyrics.size()) {
                    rslt.length = cur_metadata.lyrics[idx + 1].millisecond - cur_metadata.lyrics[idx].millisecond;
                } else {
                    rslt.length = 20000; // default
                }
                rslt.line = cur_metadata.lyrics[idx].line;
            }
        }

        return rslt;
    }

    void reset() {
        cur_track = { 0 };
        cur_position = { 0 };
        cursor = -1;
    }

private:
    CD::Player::TrackNo cur_track = {0};
    CD::Track cur_metadata;
    MSF cur_position = {0};
    int cursor = -1;
};