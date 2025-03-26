#pragma once
#include <mode.h>
#include <lyric_player.h>
#include <scrobbler.h>

using Platform::Button;

class CDMode: public Mode {
public:
    CDMode(const PlatformSharedResources res, ModeHost *);
    ~CDMode();
    void setup() override;
    UI::View& main_view() override;
    void loop() override;
    void on_remote_key_pressed(VirtualKey key) override;
    void teardown() override;
private:
    class CDPView;
    LyricPlayer lrc;
    Scrobbler * scrobbler;
    CDPView * rootView;
    CD::Player player;
    CD::CachingMetadataAggregateProvider meta;
    
    Button stopEject;
    Button playPause;
    Button forward;
    Button rewind;
    Button nextTrackDisc;
    Button prevTrackDisc;
    Button playMode;

    bool must_show_title_stopped = false;
    bool seek_from_button = false;
    bool lyrics_enabled = true;
    void update_title(const std::shared_ptr<CD::Album> disc, const CD::Track& metadata, const CD::Player::TrackNo trk);

    void prev_trk_button();
    void next_trk_button();

    TickType_t digit_timeout = pdMS_TO_TICKS(1000);
    TickType_t last_digit_time = 0;
    int entered_digits = 0;
    void commit_entered_digits();
};