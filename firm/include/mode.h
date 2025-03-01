#pragma once
#include "platform.h"
#include <esper-cdp/player.h>
#include <esper-core/spdif.h>

class ModeHost;

class Mode {
public:
    Mode(const PlatformSharedResources res, ModeHost * host): resources(res), _host(host) {}
    virtual void setup() {};
    virtual UI::View& main_view();
    virtual void loop() {};
    virtual void teardown() {};
protected:
    const PlatformSharedResources resources;
private:
    ModeHost * const _host;
};
