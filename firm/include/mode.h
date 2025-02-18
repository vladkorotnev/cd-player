#pragma once
#include "platform.h"
#include "../src/mode/cd_mode/lyric_player.h" // TODO move?
#include <esper-cdp/player.h>
#include <esper-core/spdif.h>

class Mode {
public:
    Mode(const PlatformSharedResources res): resources(res) {}
    virtual void setup() {};
    virtual UI::View& main_view();
    virtual void loop() {};
    virtual void teardown() {};
protected:
    const PlatformSharedResources resources;
};

class CDMode: public Mode {
public:
    CDMode(const PlatformSharedResources res);
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
};