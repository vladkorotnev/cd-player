#include <modes/cd_mode.h>
#include <shared_prefs.h>
#include <consts.h>
#include "cd_mode/time_bar.h"
#include "cd_mode/lyric_label.h"
#include <esper-gui/views/framework.h>
#include <views/wifi_icon.h>
#include "Arduino.h"
#include <localize.h>
#include <string>
#include <last_fm.h>

static const char LOG_TAG[] = "APL_CDP";

using CD::Player;

static const std::string HumanReadablePlayerStateString(Player::State sts) {
    switch(sts) {
        case Player::State::INIT: return localized_string("Please Wait");
        case Player::State::LOAD: return localized_string("Reading");
        case Player::State::CLOSE: return localized_string("Reading");
        case Player::State::NO_DISC: return localized_string("No Disc");
        case Player::State::BAD_DISC: return localized_string("No Disc");
        case Player::State::OPEN: return localized_string("Open");
        case Player::State::CHANGE_DISC: return localized_string("Changing Disc");
        case Player::State::STOP: return localized_string("Stop");
        default:
            return Player::PlayerStateString(sts);
            break;
    }
}

static const uint8_t shuffle_icon_data[] = {
    0b11000010,
    0b00101111,
    0b00010000,
    0b00101111,
    0b11000010,
};

static const EGImage shuffle_icon = {
    .format = EG_FMT_HORIZONTAL,
    .size = {8, 5},
    .data = shuffle_icon_data
};

class CDMode::CDPView: public UI::View {
public:
    std::shared_ptr<UI::Label> lblSmallTop;
    std::shared_ptr<UI::Label> lblBigMiddle;
    std::shared_ptr<LyricLabel> lblLyric;
    std::shared_ptr<UI::View> allButLyric;
    std::shared_ptr<TimeBar> timeBar;
    std::shared_ptr<UI::TinySpinner> loading;
    std::shared_ptr<UI::Label> lblTrackIndicator;
    std::shared_ptr<UI::Label> lblTrackInputField;
    std::shared_ptr<UI::Label> lblDiscIndicator;
    std::shared_ptr<UI::ImageView> imgShuffleIcon;
    std::shared_ptr<WiFiIcon> wifi;

    CDPView(): View({EGPointZero, DISPLAY_SIZE}) {
        allButLyric = std::make_shared<UI::View>(EGRect {EGPointZero, {160, 27}});
        lblSmallTop = std::make_shared<UI::Label>(EGRect {EGPointZero, {160, 8}}, Fonts::FallbackWildcard8px, UI::Label::Alignment::Center);
        lblBigMiddle = std::make_shared<UI::Label>(EGRect {{0, 8}, {160, 16}}, Fonts::FallbackWildcard16px, UI::Label::Alignment::Center);
        lblSmallTop->auto_scroll = true;
        lblBigMiddle->auto_scroll = true;
        lblTrackIndicator = std::make_shared<UI::Label>(EGRect {{140, 27}, {20, 5}}, Fonts::TinyDigitFont, UI::Label::Alignment::Right);
        lblTrackInputField = std::make_shared<UI::Label>(EGRect {{140, 27}, {20, 5}}, Fonts::TinyDigitFont, UI::Label::Alignment::Right);
        lblTrackInputField->hidden = true;
        lblDiscIndicator = std::make_shared<UI::Label>(EGRect {{5, 27}, {12, 5}}, Fonts::TinyDigitFont, UI::Label::Alignment::Left);
        imgShuffleIcon = std::make_shared<UI::ImageView>(&shuffle_icon, EGRect {{152, 27}, {8, 5}});
        imgShuffleIcon->hidden = true;
        lblLyric = std::make_shared<LyricLabel>(EGRect {EGPointZero, {160, 27}});
        lblLyric->hidden = true;
        timeBar = std::make_shared<TimeBar>(EGRect {{20, 27}, {120, 5}});

        wifi = std::make_shared<WiFiIcon>(EGRect {{0, 32 - 5}, {5, 5}});
        loading = std::make_shared<UI::TinySpinner>(EGRect {{0, 32 - 5}, {5, 5}});
        loading->hidden = true;

        allButLyric->subviews.push_back(lblSmallTop);
        allButLyric->subviews.push_back(lblBigMiddle);

        lblSmallTop->synchronize_scrolling_to(&lblBigMiddle);
        lblBigMiddle->synchronize_scrolling_to(&lblSmallTop);

        subviews.push_back(wifi);
        subviews.push_back(loading);
        subviews.push_back(timeBar);
        subviews.push_back(lblTrackIndicator);
        subviews.push_back(lblTrackInputField);
        subviews.push_back(imgShuffleIcon);
        subviews.push_back(lblDiscIndicator);
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

CDMode::CDMode(const PlatformSharedResources res, ModeHost * host): 
        meta { CD::CachingMetadataAggregateProvider(META_CACHE_PREFIX) },
        player { Player(res.cdrom, &meta) },
        stopEject { Button(resources.keypad, (1 << 0)) },
        playPause { Button(resources.keypad, (1 << 1)) },
        rewind { Button(resources.keypad, (1 << 2)) },
        forward { Button(resources.keypad, (1 << 3)) },
        prevTrackDisc { Button(resources.keypad, (1 << 4)) },
        nextTrackDisc { Button(resources.keypad, (1 << 5)) },
        playMode { Button(resources.keypad, (1 << 6)) },
        scrobbler(nullptr),
    Mode(res, host) {

    meta.cache_enabled = Prefs::get(PREFS_KEY_CD_CACHE_META);
    meta.providers.push_back(new CD::CDTextMetadataProvider());
    if(Prefs::get(PREFS_KEY_CD_MUSICBRAINZ_ENABLED)) 
        meta.providers.push_back(new CD::MusicBrainzMetadataProvider());
    if(Prefs::get(PREFS_KEY_CD_CDDB_ENABLED))
        meta.providers.push_back(new CD::CDDBMetadataProvider(Prefs::get(PREFS_KEY_CDDB_ADDRESS), Prefs::get(PREFS_KEY_CDDB_EMAIL)));

    // For now those are manually sorted in the order of quality as perceived by me
    // Someday reordering might be good but for now fuck it
    if(Prefs::get(PREFS_KEY_CD_LRCLIB_ENABLED))
        meta.providers.push_back(new CD::LrcLibLyricProvider());
    if(Prefs::get(PREFS_KEY_CD_NETEASE_ENABLED))
        meta.providers.push_back(new CD::NeteaseLyricProvider());
    if(Prefs::get(PREFS_KEY_CD_QQ_ENABLED))
        meta.providers.push_back(new CD::QQMusicLyricProvider());

    if(Prefs::get(PREFS_KEY_CD_LASTFM_ENABLED)) {
        auto user = Prefs::get(PREFS_KEY_CD_LASTFM_USER);
        auto pass = Prefs::get(PREFS_KEY_CD_LASTFM_PASS);
        if(!user.empty() && !pass.empty()) {
            #if defined(LASTFM_API_KEY) && defined(LASTFM_API_SECRET)
            scrobbler = new ThreadedScrobbler(LastFMScrobbler(LASTFM_API_KEY, LASTFM_API_SECRET, user, pass));
            #else
            #warning "No API keys for LAST.FM, scrobbling will not work!"
            #endif
        }
    }

    lyrics_enabled = Prefs::get(PREFS_KEY_CD_LYRICS_ENABLED);

    rootView = new CDPView();
}

CDMode::~CDMode() {
    delete rootView;
    if(scrobbler != nullptr) {
        delete scrobbler;
    }
}

void CDMode::setup() {
    resources.router->activate_route(Platform::AudioRoute::ROUTE_SPDIF_CD_PORT);
}

void CDMode::update_title(const std::shared_ptr<CD::Album> disc, const CD::Track& metadata, const Player::TrackNo trk) {
    rootView->imgShuffleIcon->hidden = (player.get_play_mode() != Player::PlayMode::PLAYMODE_SHUFFLE || player.get_status() == Player::State::STOP);
    if(disc->tracks.size() == 0 || metadata.title == "") {
        rootView->lblTrackIndicator->hidden = true;
    } else {
        rootView->lblTrackIndicator->hidden = !rootView->imgShuffleIcon->hidden;
        if(trk.index < 2) {
            rootView->lblTrackIndicator->set_value(std::to_string(trk.track) + "/" + std::to_string(disc->tracks.size()));
        } else {
            rootView->lblTrackIndicator->set_value(std::to_string(trk.track) + "." + std::to_string(trk.index));
        }
    }

    if(metadata.artist != "" && metadata.artist != disc->artist) {
        rootView->lblSmallTop->set_value(metadata.artist);
        rootView->lblSmallTop->hidden = false;
    } else {
        rootView->lblSmallTop->hidden = true;
    }

    if(metadata.title != "") {
        rootView->lblBigMiddle->set_value(metadata.title);
    } else {
        if(trk.index < 2) {
            rootView->lblBigMiddle->set_value(localized_string("Track") + " " + std::to_string(trk.track) + "/" + std::to_string(disc->tracks.size()));
        } else {
            rootView->lblBigMiddle->set_value(localized_string("Track") + " " + std::to_string(trk.track) + "-" + std::to_string(trk.index) + "/" + std::to_string(disc->tracks.size()));
        }
    }
}

void CDMode::loop() {
    static uint8_t kp_sts = 0;

    auto const& trk = player.get_current_track_number();
    auto const sts = player.get_status();
    auto const& msf_now = player.get_current_track_time();
    auto const& disc = player.get_active_slot().disc;
    auto const& tracklist = disc->tracks;

    rootView->loading->hidden = !(sts == Player::State::INIT || sts == Player::State::LOAD || sts == Player::State::CLOSE || sts == Player::State::CHANGE_DISC || player.is_processing_metadata());
    rootView->wifi->hidden = !rootView->loading->hidden;

    rootView->timeBar->hidden = !(sts == Player::State::PLAY || sts == Player::State::PAUSE || sts == Player::State::SEEK_FF || sts == Player::State::SEEK_REW);
    rootView->timeBar->trackbar->filled = (sts == Player::State::PLAY);
    rootView->timeBar->trackbar->blinking = (sts == Player::State::PAUSE);

    if(player.get_slots().size() > 1) {
        rootView->lblDiscIndicator->set_value(std::to_string(player.get_active_slot_index() + 1) + "/" + std::to_string(player.get_slots().size()));
        rootView->lblDiscIndicator->hidden = false;
    } else {
        rootView->lblDiscIndicator->hidden = true;
    }

    rootView->lblTrackInputField->hidden = (entered_digits == 0 || ((xTaskGetTickCount() - last_digit_time) > digit_timeout));
    if(!rootView->lblTrackInputField->hidden) {
        rootView->lblTrackInputField->set_value(std::to_string(entered_digits) + "-");
    }

    switch(sts) {
        case Player::State::PLAY:
        case Player::State::PAUSE:
        case Player::State::SEEK_FF:
        case Player::State::SEEK_REW:
            if(tracklist.size() >= trk.track && trk.track > 0) {
                auto metadata = tracklist[trk.track - 1];
                update_title(disc, metadata, trk);
                rootView->timeBar->update_msf(metadata.disc_position.position, msf_now, metadata.duration, trk.index == 0);
            }
            must_show_title_stopped = false;
            break;

        case Player::State::STOP:
            rootView->set_lyric_show(false, 0);
            if(must_show_title_stopped && tracklist.size() >= trk.track && trk.track > 0) {
                auto metadata = tracklist[trk.track - 1];
                update_title(disc, metadata, trk);
            } else {
                rootView->lblTrackIndicator->hidden = true;
                rootView->imgShuffleIcon->hidden = (player.get_play_mode() != Player::PlayMode::PLAYMODE_SHUFFLE);
                if(disc->title != "") {
                    rootView->lblBigMiddle->set_value(disc->title);
                } else {
                    rootView->lblBigMiddle->set_value(HumanReadablePlayerStateString(sts));
                }
                if(disc->artist != "") {
                    rootView->lblSmallTop->set_value(disc->artist);
                    rootView->lblSmallTop->hidden = false;
                } else {
                    rootView->lblSmallTop->hidden = true;
                }
            }
            break;

        case Player::State::BAD_DISC:
        case Player::State::NO_DISC:
        case Player::State::CHANGE_DISC:
        case Player::State::OPEN:
        case Player::State::CLOSE:
        case Player::State::LOAD:
        case Player::State::INIT:
            must_show_title_stopped = false;
            rootView->lblTrackIndicator->hidden = true;
            rootView->imgShuffleIcon->hidden = true;
            rootView->lblSmallTop->hidden = true;
            rootView->lblBigMiddle->set_value(HumanReadablePlayerStateString(sts));
            entered_digits = 0;
            break;
    }

    if(entered_digits != 0 && (xTaskGetTickCount() - last_digit_time) > digit_timeout) {
        commit_entered_digits();
    }

    if(sts != Player::State::PLAY) {
        rootView->set_lyric_show(false, 0);
        lrc.reset();
    } else if(tracklist.size() >= trk.track && trk.track > 0 && trk.index > 0) {
        auto metadata = tracklist[trk.track - 1];
        lrc.feed_track(trk, metadata);
        const auto line = lrc.feed_position(msf_now);
        if(lyrics_enabled) {
            if (!line.line.empty() && line.length > 0) {
                rootView->lblLyric->set_value(line.line);
                rootView->set_lyric_show(true, line.length + 5000);
            }
            else if (line.must_clear) {
                rootView->set_lyric_show(false, 0);
            }
        }

        if(scrobbler != nullptr) {
            scrobbler->feed_track(trk, metadata);
            scrobbler->feed_position(msf_now);
        }
    }

    // now you know why we have constraints autolayout all that shite in the "real world", duh
    rootView->lblBigMiddle->frame = (rootView->lblSmallTop->hidden && !rootView->timeBar->hidden && (!rootView->lblTrackIndicator->hidden || !rootView->imgShuffleIcon->hidden)) ? EGRect {EGPointZero, {160, 24}} : EGRect {{0, 8}, {160, 16}};

    if(stopEject.is_clicked()) {
        player.do_command((sts == Player::State::PLAY || sts == Player::State::PAUSE || sts == Player::State::SEEK_FF || sts == Player::State::SEEK_REW) ? Player::Command::STOP : Player::Command::OPEN_CLOSE);
    }
    else if(playPause.is_clicked()) {
        player.do_command((sts == Player::State::PLAY) ? Player::Command::PAUSE : Player::Command::PLAY);
        rootView->timeBar->trackbar->filled = (sts == Player::State::PLAY);
        rootView->timeBar->set_needs_display();
    }
    else if(forward.is_pressed() && sts != Player::State::SEEK_FF && sts != Player::State::SEEK_REW) {
        player.do_command(Player::Command::SEEK_FF);
        seek_from_button = true;
    }
    else if(rewind.is_pressed() && sts != Player::State::SEEK_FF && sts != Player::State::SEEK_REW) {
        player.do_command(Player::Command::SEEK_REW);
        seek_from_button = true;
    }
    else if(((!forward.is_pressed() && sts == Player::State::SEEK_FF) || (!rewind.is_pressed() && sts == Player::State::SEEK_REW)) && seek_from_button) {
        player.do_command(Player::Command::END_SEEK);
        seek_from_button = false;
    }
    else if(prevTrackDisc.is_clicked()) {
        prev_trk_button();
    }
    else if(prevTrackDisc.is_held()) {
        player.do_command(Player::Command::PREV_DISC);
    }
    else if(nextTrackDisc.is_clicked()) {
        next_trk_button();
    }
    else if(nextTrackDisc.is_held()) {
        player.do_command(Player::Command::NEXT_DISC);
    }
    else if(playMode.is_clicked()) {
        if(player.get_play_mode() == Player::PlayMode::PLAYMODE_SHUFFLE) {
            player.set_play_mode(Player::PlayMode::PLAYMODE_CONTINUE);
        } else {
            player.set_play_mode(Player::PlayMode::PLAYMODE_SHUFFLE);
        }
    }

    // any key cancels lyrics
    uint8_t kp_new_sts = resources.keypad->get_value();
    if(kp_new_sts != kp_sts) {
        rootView->set_lyric_show(false, 0);
        kp_sts = kp_new_sts;
    }

    delay(10);
}

void CDMode::prev_trk_button() {
    auto const sts = player.get_status();
    if(sts != Player::State::STOP || must_show_title_stopped) player.do_command(Player::Command::PREV_TRACK);
    if(sts == Player::State::STOP) must_show_title_stopped = true;
}

void CDMode::next_trk_button() {
    auto const sts = player.get_status();
    if(sts != Player::State::STOP || must_show_title_stopped) player.do_command(Player::Command::NEXT_TRACK);
    if(sts == Player::State::STOP) must_show_title_stopped = true;
}

void CDMode::on_remote_key_pressed(VirtualKey key) {
    // any key cancels lyrics
    rootView->set_lyric_show(false, 0);

    const std::unordered_map<VirtualKey, Player::Command> key_to_cmd = {
        {RVK_DISK_NEXT, Player::Command::NEXT_DISC},
        {RVK_DISK_PREV, Player::Command::PREV_DISC},
        {RVK_SEEK_FWD, Player::Command::SEEK_FF},
        {RVK_SEEK_BACK, Player::Command::SEEK_REW},
        {RVK_PLAY, Player::Command::PLAY},
        {RVK_PAUSE, Player::Command::PAUSE},
        {RVK_STOP, Player::Command::STOP},
        {RVK_EJECT, Player::Command::OPEN_CLOSE}
    };
    
    auto const cmd = key_to_cmd.find(key);
    if((key == RVK_CURS_ENTER || key == RVK_PLAY) && entered_digits != 0) {
        commit_entered_digits();
    }
    else if(cmd != key_to_cmd.cend()) {
        player.do_command(cmd->second);
    }
    else if(key == RVK_SHUFFLE) {
        if(player.get_play_mode() == Player::PlayMode::PLAYMODE_SHUFFLE) {
            player.set_play_mode(Player::PlayMode::PLAYMODE_CONTINUE);
        } else {
            player.set_play_mode(Player::PlayMode::PLAYMODE_SHUFFLE);
        }
    }
    else if(key == RVK_TRACK_NEXT) {
        next_trk_button();
    }
    else if(key == RVK_TRACK_PREV) {
        prev_trk_button();
    }
    else if(key == RVK_LYRICS) {
        lyrics_enabled = true;
    }
    else if(key == RVK_DISP) {
        lyrics_enabled = false;
        rootView->set_lyric_show(false, 0);
    }
    else if(RVK_IS_DIGIT(key)) {
        int digit = RVK_TO_DIGIT(key);
        auto const& disc = player.get_active_slot().disc;
        TickType_t now = xTaskGetTickCount();
        if(now - last_digit_time >= digit_timeout || last_digit_time == 0) {
            entered_digits = 0;
        }
        last_digit_time = now;
        entered_digits *= 10;
        entered_digits += digit;

        if(entered_digits > disc->tracks.size()) {
            // no such track on disc, is the user fixing a typo?
            entered_digits = digit;
        }

        if(entered_digits * 10 > disc->tracks.size()) {
            // e.g. entered 2 on a 15 track disc: 2*10 = 20, >15, so clearly user wants track 2, so navigate to it directly
            // BUT entered 1 on a 15 track disc: 1*10 = 10, <15, maybe the user wants track 13? so wait for next input
            commit_entered_digits();
        }
    }
}

void CDMode::commit_entered_digits() {
    if(player.get_status() == Player::State::STOP) must_show_title_stopped = true;
    player.navigate_to_track(entered_digits);
    last_digit_time = 0;
    entered_digits = 0;
}

void CDMode::teardown() {
    player.do_command(Player::Command::STOP);

    resources.router->activate_route(Platform::AudioRoute::ROUTE_NONE_INACTIVE);

    while(!meta.providers.empty()) {
        delete meta.providers.back();
        meta.providers.pop_back();
    }

    player.teardown_tasks();
    player.power_down();
}

UI::View& CDMode::main_view() {
    return *rootView;
}