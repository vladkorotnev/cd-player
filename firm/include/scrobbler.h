#pragma once

#include <esper-cdp/types.h>
#include <esper-cdp/player.h>
#include <esp32-hal-log.h>

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
            scrobble_at = 0;
            scrobble_at = std::min(MSF_TO_FRAMES((MSF { .M = 4, .S = 0, .F = 0 })), MSF_TO_FRAMES(cur_metadata.duration) / 2);

            if(cur_metadata.title != "") {
                do_now_playing(cur_metadata);
            }
        }
    }

    void feed_position(MSF pos) {
        if(scrobbled) return;
        if(MSF_TO_FRAMES(pos) >= scrobble_at) {
            if(MSF_TO_MILLIS(cur_metadata.duration) > 30000 && cur_metadata.title != "")
                do_scrobble(cur_metadata);
            scrobbled = true;
        }
    }

    void reset() {
        cur_track = { 0 };
        scrobbled = false;
        scrobble_at = 0;
    }

protected:
    virtual void do_scrobble(const CD::Track& meta) {};
    virtual void do_now_playing(const CD::Track& meta) {};

private:
    CD::Player::TrackNo cur_track = {0};
    CD::Track cur_metadata;
    int scrobble_at = 0;
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
    void do_scrobble(const CD::Track& meta) override {
        ScrobbleTask tmp;
        tmp.kind = ScrobbleTaskKind::Scrobble;
        tmp.meta = meta;
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
                        that->innerScrobbler->do_scrobble(tmp.meta);
                        break;
                }
            }
        }
        vTaskDelete(NULL);
    }
};
