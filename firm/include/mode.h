#pragma once
#include "platform.h"
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
