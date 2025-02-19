#include <mode.h>
#include "cd_mode/time_bar.h"
#include "cd_mode/lyric_label.h"
#include <esper-gui/views/framework.h>
#include "Arduino.h"
#include <string>

static const char LOG_TAG[] = "APL_CDP";
using CD::Player;


class CDMode::CDPView: public UI::View {
public:

    std::shared_ptr<UI::Label> lblSmallTop;
    std::shared_ptr<UI::Label> lblBigMiddle;
    std::shared_ptr<LyricLabel> lblLyric;
    std::shared_ptr<UI::View> allButLyric;
    std::shared_ptr<TimeBar> timeBar;
    std::shared_ptr<UI::TinySpinner> loading;

    CDPView(): View({EGPointZero, DISPLAY_SIZE}) {
        allButLyric = std::make_shared<UI::View>(UI::View({{0, 0}, {160, 27}}));
        lblSmallTop = std::make_shared<UI::Label>(UI::Label({{0, 0}, {160, 8}}, Fonts::FallbackWildcard8px, UI::Label::Alignment::Center));
        lblBigMiddle = std::make_shared<UI::Label>(UI::Label({{0, 8}, {160, 16}}, Fonts::FallbackWildcard16px, UI::Label::Alignment::Center));
        lblSmallTop->auto_scroll = true;
        lblBigMiddle->auto_scroll = true;

        lblLyric = std::make_shared<LyricLabel>(LyricLabel({{0, 0}, {160, 27}}));
        lblLyric->hidden = true;
        timeBar = std::make_shared<TimeBar>(TimeBar({{0, 27}, {160, 5}}));

        loading = std::make_shared<UI::TinySpinner>(UI::TinySpinner({{160 - 6, 32 - 6}, {5, 5}}));
        loading->hidden = true;

        allButLyric->subviews.push_back(lblSmallTop);
        allButLyric->subviews.push_back(lblBigMiddle);

        subviews.push_back(loading);
        subviews.push_back(timeBar);
        subviews.push_back(lblLyric);
        subviews.push_back(allButLyric);
    }

    bool needs_display() override {
        if(xTaskGetTickCount() - last_lyric_time >= pdMS_TO_TICKS(lyric_len)) {
            set_lyric_show(false, lyric_len);
        }
        return View::needs_display();
    }

    void set_lyric_show(bool is_lyric, int len) {
        if(is_lyric) last_lyric_time = xTaskGetTickCount();

        if(now_is_lyric != is_lyric) {
            lyric_len = len;
            lblLyric->hidden = !is_lyric;
            allButLyric->hidden = is_lyric;
            set_needs_display();
            now_is_lyric = is_lyric;
        }
    }
private:
    bool now_is_lyric = false;
    int lyric_len = 0;
    TickType_t last_lyric_time = 0;
};

CDMode::CDMode(const PlatformSharedResources res): 
        ide { Platform::IDEBus(res.i2c) },
        cdrom { ATAPI::Device(&ide) },
        meta { CD::CachingMetadataAggregateProvider("/mnt/cddb") },
        player { Player(&cdrom, &meta) },
        Mode(res) {

    meta.providers.push_back(new CD::CDTextMetadataProvider());
    meta.providers.push_back(new CD::MusicBrainzMetadataProvider());
    meta.providers.push_back(new CD::CDDBMetadataProvider("gnudb.gnudb.org", "asdfasdf@example-esp32.com"));
    meta.providers.push_back(new CD::LrcLibLyricProvider());

    rootView = new CDPView();
}

void CDMode::setup() {
    resources.router->activate_route(Platform::AudioRoute::ROUTE_SPDIF_CD_PORT);
}

void CDMode::update_title(std::shared_ptr<CD::Album> disc, const CD::Track metadata, Player::TrackNo trk) {
    if(metadata.artist != "" && metadata.artist != disc->artist) {
        rootView->lblSmallTop->set_value(metadata.artist);
        rootView->lblSmallTop->hidden = false;
    } else {
        if(metadata.title != "") {
            rootView->lblSmallTop->hidden = false;
            if(trk.index < 2) {
                rootView->lblSmallTop->set_value("Track " + std::to_string(trk.track));
            } else {
                rootView->lblSmallTop->set_value("Track " + std::to_string(trk.track) + "-" + std::to_string(trk.index));
            }
        }
        else {
            rootView->lblSmallTop->hidden = true;
        }
    }
    if(metadata.title != "") {
        rootView->lblBigMiddle->set_value(metadata.title);
    } else {
        if(trk.index < 2) {
            rootView->lblBigMiddle->set_value("Track " + std::to_string(trk.track));
        } else {
            rootView->lblBigMiddle->set_value("Track " + std::to_string(trk.track) + " - " + std::to_string(trk.index));
        }
    }
}

void CDMode::loop() {
    static uint8_t kp_sts = 0;

    auto trk = player.get_current_track_number();
    auto sts = player.get_status();
    auto msf_now = player.get_current_track_time();
    auto disc = player.get_active_slot().disc;
    auto tracklist = disc->tracks;

    rootView->loading->hidden = !(sts == Player::State::INIT || sts == Player::State::LOAD || sts == Player::State::CLOSE || sts == Player::State::CHANGE_DISC || player.is_processing_metadata());
    rootView->timeBar->hidden = !(sts == Player::State::PLAY || sts == Player::State::PAUSE || sts == Player::State::SEEK_FF || sts == Player::State::SEEK_REW);
    rootView->timeBar->trackbar->filled = (sts == Player::State::PLAY);

    switch(sts) {
        case Player::State::PLAY:
        case Player::State::PAUSE:
        case Player::State::SEEK_FF:
        case Player::State::SEEK_REW:
            if(tracklist.size() >= trk.track && trk.track > 0) {
                auto metadata = tracklist[trk.track - 1];
                update_title(disc, metadata, trk);
                MSF next_pos = (trk.track == tracklist.size()) ? disc->duration : tracklist[trk.track].disc_position.position;
                rootView->timeBar->update_msf(metadata.disc_position.position, msf_now, next_pos, trk.index == 0);
            }
            must_show_title_stopped = false;
            break;

        case Player::State::STOP:
            rootView->set_lyric_show(false, 0);
            if(must_show_title_stopped && tracklist.size() >= trk.track && trk.track > 0) {
                auto metadata = tracklist[trk.track - 1];
                update_title(disc, metadata, trk);
            } else {
                if(disc->title != "") {
                    rootView->lblBigMiddle->set_value(disc->title);
                } else {
                    rootView->lblBigMiddle->set_value(CD::Player::PlayerStateString(sts));
                }
                if(disc->artist != "") {
                    rootView->lblSmallTop->set_value(disc->artist);
                    rootView->lblSmallTop->hidden = false;
                } else {
                    rootView->lblSmallTop->hidden = true;
                }
            }
            break;

        case Player::State::CHANGE_DISC:
            must_show_title_stopped = false;
            break;

        case Player::State::BAD_DISC:
            sts = Player::State::NO_DISC;
        case Player::State::NO_DISC:
        case Player::State::OPEN:
        case Player::State::CLOSE:
        case Player::State::LOAD:
        case Player::State::INIT:
            must_show_title_stopped = false;
            rootView->lblSmallTop->hidden = true;
            rootView->lblBigMiddle->set_value(CD::Player::PlayerStateString(sts));
            break;
    }

    if(sts != Player::State::PLAY) {
        rootView->set_lyric_show(false, 0);
        lrc.reset();
    } else if(tracklist.size() >= trk.track && trk.track > 0) {
        auto metadata = tracklist[trk.track - 1];
        lrc.feed_track(trk, metadata);
        int line_len = 0;
        auto line = lrc.feed_position(msf_now, &line_len);
        if(!line.empty()) {
            rootView->lblLyric->set_value(line);
            rootView->set_lyric_show(true, line_len + 5000);
        }
    }

    uint8_t kp_new_sts = 0;
    if(resources.keypad->read(&kp_new_sts)) {
        if(kp_new_sts != kp_sts) {
            ESP_LOGI("Test", "Key change: 0x%02x to 0x%02x", kp_sts, kp_new_sts);
            rootView->set_lyric_show(false, 0);
            kp_sts = kp_new_sts;
            
            if(kp_sts == 1) {
                player.do_command((sts == Player::State::PLAY || sts == Player::State::PAUSE || sts == Player::State::SEEK_FF || sts == Player::State::SEEK_REW) ? Player::Command::STOP : Player::Command::OPEN_CLOSE);
            }
            else if(kp_sts == 2) {
                player.do_command(CD::Player::Command::PLAY_PAUSE);
            }
            else if(kp_sts == 4) { 
                player.do_command(CD::Player::Command::PREV_TRACK);
                if(sts == Player::State::STOP) must_show_title_stopped = true;
            }
            else if(kp_sts == 8) { 
                player.do_command(CD::Player::Command::NEXT_TRACK);
                if(sts == Player::State::STOP) must_show_title_stopped = true;
            }
            else if(kp_sts == 16) { 
                player.do_command(CD::Player::Command::PREV_DISC);
            }
            else if(kp_sts == 32) {
                player.do_command(CD::Player::Command::NEXT_DISC);
            }
            else if(kp_sts == 64) {
                player.do_command(CD::Player::Command::SEEK_FF);
            }
            else if(kp_sts == 128) {
                player.do_command(CD::Player::Command::END_SEEK);
            }
        }
    }
}

void CDMode::teardown() {
    if(player.get_status() == Player::State::OPEN) {
        player.do_command(Player::Command::CLOSE);
    } else {
        player.do_command(Player::Command::STOP);
    }

    resources.router->activate_route(Platform::AudioRoute::ROUTE_NONE_INACTIVE);

    while(!meta.providers.empty()) {
        delete meta.providers.back();
        meta.providers.pop_back();
    }

    delete rootView;
}

UI::View& CDMode::main_view() {
    return *rootView;
}