#include <mode.h>
#include <esper-cdp/lyrics.h>
#include <esper-gui/views/framework.h>
#include "Arduino.h"
#include <string>

static const char LOG_TAG[] = "APL_CDP";
using CD::Player;

class CDMode::CDPView: public UI::View {
public:

    std::shared_ptr<UI::Label> lblStatus;
    std::shared_ptr<UI::Label> lblTrk;
    std::shared_ptr<UI::Label> lblTimeElapsed;
    std::shared_ptr<UI::Label> lblTimeRemaining;
    std::shared_ptr<UI::ProgressBar> trackbar;
    
    CDPView(): View({EGPointZero, DISPLAY_SIZE}) {
        lblStatus = std::make_shared<UI::Label>(UI::Label({{0, 0}, {160, 8}}, Fonts::FallbackWildcard8px, UI::Label::Alignment::Center));
        lblTrk = std::make_shared<UI::Label>(UI::Label({{0, 8}, {160, 16}}, Fonts::FallbackWildcard16px, UI::Label::Alignment::Left));
        lblTimeElapsed = std::make_shared<UI::Label>(UI::Label({{0, 27}, {27, 5}}, Fonts::TinyDigitFont, UI::Label::Alignment::Right));
        lblTimeRemaining = std::make_shared<UI::Label>(UI::Label({{160-27, 27}, {27, 5}}, Fonts::TinyDigitFont, UI::Label::Alignment::Left));
        trackbar = std::make_shared<UI::ProgressBar>(UI::ProgressBar({{28, 27}, {160-28-26, 5}}));
        subviews.push_back(lblStatus);
        subviews.push_back(lblTrk);
        subviews.push_back(lblTimeElapsed);
        subviews.push_back(lblTimeRemaining);
        subviews.push_back(trackbar);
    }
};

CDMode::CDMode(const PlatformSharedResources res): 
        ide { Platform::IDEBus(res.i2c) },
        cdrom { ATAPI::Device(&ide) },
        meta { CD::CachingMetadataAggregateProvider("/littlefs") },
        player { Player(&cdrom, &meta) },
        Mode(res) {

    meta.providers.push_back(new CD::CDTextMetadataProvider());
    meta.providers.push_back(new CD::MusicBrainzMetadataProvider());
    meta.providers.push_back(new CD::CDDBMetadataProvider("gnudb.gnudb.org", "asdfasdf@example-esp32.com"));
    // meta.providers.push_back(new CD::LrcLibLyricProvider());

    rootView = new CDPView();
}

void CDMode::setup() {
    resources.router->activate_route(Platform::AudioRoute::ROUTE_SPDIF_CD_PORT);
}

void CDMode::loop() {
    static uint8_t kp_sts = 0;

    auto trk = player.get_current_track_number();
    auto sts = player.get_status();
    auto msf_now = player.get_current_track_time();

    if(sts == CD::Player::State::PLAY && player.get_active_slot().disc->tracks.size() >= trk.track && trk.track > 0) {
        auto metadata = player.get_active_slot().disc->tracks[trk.track - 1];
        rootView->lblStatus->set_value(metadata.artist);
        rootView->lblTrk->set_value(metadata.title);
        int cur_starts_at = MSF_TO_FRAMES(metadata.disc_position.position);
        int next_starts_at = MSF_TO_FRAMES(player.get_active_slot().disc->tracks[trk.track].disc_position.position);
        int duration_frames = next_starts_at - cur_starts_at; // TODO: FRAMES_TO_MSF not converting correctly
        int duration_sec = (duration_frames / MSF::FRAMES_IN_SECOND) % 60;
        int duration_min = (duration_frames / MSF::FRAMES_IN_SECOND) / 60;
        rootView->lblTimeElapsed->set_value((trk.index == 0 ? "-":"") + std::to_string(msf_now.M) + ":" + (msf_now.S < 10 ? "0":"") + std::to_string(msf_now.S)); // performance of this is gonna be awful innit
        rootView->lblTimeRemaining->set_value(std::to_string(duration_min) + ":" + (duration_sec < 10 ? "0":"") + std::to_string(duration_sec)); // performance of this is gonna be awful innit
        rootView->trackbar->maximum = duration_frames;
        rootView->trackbar->value = MSF_TO_FRAMES(msf_now);
    }
    else if(player.get_active_slot().disc->title != "") {
        rootView->lblTrk->set_value(player.get_active_slot().disc->title);
        rootView->lblStatus->set_value("Track #" + std::to_string(trk.track));
    }
    else {
        rootView->lblStatus->set_value(CD::Player::PlayerStateString(sts));
        rootView->lblTrk->set_value("Track #" + std::to_string(trk.track));
    }


    rootView->set_needs_display(); // TODO: fix me and remove: tiled blitting bugs out when theres something in the last pixels of framebuffer

    uint8_t kp_new_sts = 0;
    if(resources.keypad->read(&kp_new_sts)) {
        if(kp_new_sts != kp_sts) {
            ESP_LOGI("Test", "Key change: 0x%02x to 0x%02x", kp_sts, kp_new_sts);
            kp_sts = kp_new_sts;
            
            if(kp_sts == 1) {
                player.do_command(CD::Player::Command::OPEN_CLOSE);
            }
            else if(kp_sts == 2) {
                player.do_command(CD::Player::Command::PLAY_PAUSE);
            }
            else if(kp_sts == 4) { 
                player.do_command(CD::Player::Command::PREV_TRACK);
            }
            else if(kp_sts == 8) { 
                player.do_command(CD::Player::Command::NEXT_TRACK);
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