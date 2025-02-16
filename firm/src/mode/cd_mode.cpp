#include <mode.h>
#include <esper-cdp/lyrics.h>
#include <esper-gui/views/framework.h>
#include "Arduino.h"
#include <string>

using CD::Player;

class CDMode::CDPView: public UI::View {
public:

    std::shared_ptr<UI::Label> lblStatus;
    std::shared_ptr<UI::Label> lblTrk;
    std::shared_ptr<UI::Label> lblTime;
    
    CDPView(): View({EGPointZero, DISPLAY_SIZE}) {
        lblStatus = std::make_shared<UI::Label>(UI::Label({{0, 0}, {160, 8}}, Fonts::FallbackWildcard8px, "Status"));
        lblTrk = std::make_shared<UI::Label>(UI::Label({{0, 8}, {160, 16}}, Fonts::FallbackWildcard16px, "Track 0"));
        lblTime = std::make_shared<UI::Label>(UI::Label({{0, 24}, {27, 5}}, Fonts::TinyDigitFont, "0:00"));
        subviews.push_back(lblStatus);
        subviews.push_back(lblTrk);
        subviews.push_back(lblTime);
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
    if(sts == CD::Player::State::PLAY && player.get_active_slot().disc->tracks.size() >= trk.track && trk.track > 0) {
        auto metadata = player.get_active_slot().disc->tracks[trk.track - 1];
        rootView->lblStatus->set_value(metadata.artist);
        rootView->lblTrk->set_value(metadata.title);
    } else {
        rootView->lblStatus->set_value(CD::Player::PlayerStateString(player.get_status()));
        rootView->lblTrk->set_value("Track #" + std::to_string(player.get_current_track_number().track));
    }

    auto msf = player.get_current_track_time();
    rootView->lblTime->set_value((trk.index == 0 ? "-":"") + std::to_string(msf.M) + ":" + (msf.S < 10 ? "0":"") + std::to_string(msf.S)); // performance of this is gonna be awful innit
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