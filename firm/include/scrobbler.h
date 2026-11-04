#pragma once

#include <esper-cdp/types.h>
#include <esper-cdp/player.h>
#include <esp32-hal-log.h>
#include <ctime>

class Scrobbler {
friend class ThreadedScrobbler;
public:
    Scrobbler() {}
    virtual ~Scrobbler() = default;

    void feed_track(CD::Player::TrackNo trk, const CD::Track& meta) {
        if(trk.track != cur_track.track) {
            cur_metadata = meta;
            cur_track = trk;
            scrobbled = false;
            started_at = std::time(nullptr);
            listened_ticks = 0;
            playback_active = false;
            scrobble_at = std::min(pdMS_TO_TICKS(4 * 60 * 1000), pdMS_TO_TICKS(MSF_TO_MILLIS(cur_metadata.duration) / 2));

            if(cur_metadata.title != "") {
                do_now_playing(cur_metadata);
            }
        }
    }

    void feed_playback() {
        if(cur_track.track == 0) return;
        const TickType_t now = xTaskGetTickCount();
        if(playback_active) listened_ticks += now - last_play_tick;
        last_play_tick = now;
        playback_active = true;

        if(scrobbled) return;
        if(listened_ticks >= scrobble_at) {
            if(MSF_TO_MILLIS(cur_metadata.duration) > 30000 && cur_metadata.title != "")
                do_scrobble(cur_metadata, started_at);
            scrobbled = true;
        }
    }

    void pause() {
        playback_active = false;
    }

    void reset() {
        cur_track = { 0 };
        scrobbled = false;
        scrobble_at = 0;
        listened_ticks = 0;
        playback_active = false;
        started_at = 0;
    }

protected:
    virtual void do_scrobble(const CD::Track& meta, time_t started_at) {};
    virtual void do_now_playing(const CD::Track& meta) {};

private:
    CD::Player::TrackNo cur_track = {0};
    CD::Track cur_metadata;
    TickType_t scrobble_at = 0;
    TickType_t last_play_tick = 0;
    uint64_t listened_ticks = 0;
    time_t started_at = 0;
    bool playback_active = false;
    bool scrobbled = false;
};

class ThreadedScrobbler: public Scrobbler {
public:
    template <typename T>
    ThreadedScrobbler(T&& scrobbler):
        Scrobbler(),
        queue(),
        queueSemaphore(xSemaphoreCreateCounting(4, 0)),
        innerScrobbler(std::make_unique<T>(std::move(scrobbler))),
        scrobbleTask(NULL)
        {
            xTaskCreate(
                taskFunc,
                "SCROBBLE",
                8000,
                this,
                1,
                &scrobbleTask   
            );
        }

    ~ThreadedScrobbler() override {
        if(scrobbleTask != NULL) {
            vTaskDelete(scrobbleTask);
        }
        vSemaphoreDelete(queueSemaphore);
    }

protected:
    void do_scrobble(const CD::Track& meta, time_t started_at) override {
        ScrobbleTask tmp;
        tmp.kind = ScrobbleTaskKind::Scrobble;
        tmp.meta = meta;
        tmp.started_at = started_at;
        queue.push(tmp);
        xSemaphoreGive(queueSemaphore);
    }

    void do_now_playing(const CD::Track& meta) override {
        ScrobbleTask tmp;
        tmp.kind = ScrobbleTaskKind::NowPlaying;
        tmp.meta = meta;
        queue.push(tmp);
        xSemaphoreGive(queueSemaphore);
    }

private:
    enum class ScrobbleTaskKind {
        Scrobble,
        NowPlaying
    };
    struct ScrobbleTask {
        CD::Track meta;
        ScrobbleTaskKind kind;
        time_t started_at = 0;
    };
    SemaphoreHandle_t queueSemaphore;
    TaskHandle_t scrobbleTask;
    std::queue<ScrobbleTask> queue;
    std::unique_ptr<Scrobbler> innerScrobbler;

    static void taskFunc(void* pvParameter) {
        ThreadedScrobbler * that = static_cast<ThreadedScrobbler*>(pvParameter);
        while(1) {
            if(xSemaphoreTake(that->queueSemaphore, portMAX_DELAY)) {
                if(that->queue.empty()) continue;
                ScrobbleTask tmp = that->queue.front();
                that->queue.pop();
                ESP_LOGI("ThreadedScrobbler", "Task: %s, Artist: [%s], Title: [%s]", (tmp.kind == ScrobbleTaskKind::Scrobble) ? "Scrobble" : "NowPlaying", tmp.meta.artist.c_str(), tmp.meta.title.c_str());
                switch(tmp.kind) {
                    case ScrobbleTaskKind::NowPlaying:
                        that->innerScrobbler->do_now_playing(tmp.meta);
                        break;
                    case ScrobbleTaskKind::Scrobble:
                        that->innerScrobbler->do_scrobble(tmp.meta, tmp.started_at);
                        break;
                }
            }
        }
        vTaskDelete(NULL);
    }
};
