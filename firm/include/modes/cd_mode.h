#pragma once
#include <mode.h>
#include <lyric_player.h>

using Platform::Button;

class CDMode: public Mode {
public:
    CDMode(const PlatformSharedResources res);
    ~CDMode();
    void setup() override;
    UI::View& main_view() override;
    void loop() override;
    void teardown() override;
private:
    class CDPView;
    LyricPlayer lrc;
    CDPView * rootView;
    Platform::IDEBus ide;
    ATAPI::Device cdrom;
    CD::Player player;
    CD::CachingMetadataAggregateProvider meta;
    
    Button stopEject;
    Button playPause;
    Button forward;
    Button rewind;
    Button nextTrackDisc;
    Button prevTrackDisc;
    Button playMode;
    Button chgSource;

    bool must_show_title_stopped = false;
    void update_title(const std::shared_ptr<CD::Album> disc, const CD::Track& metadata, const CD::Player::TrackNo trk);
};