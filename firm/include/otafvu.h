#pragma once
#include <functional>
#include <mode_host.h>

namespace OTAFVU {
    // returns true if recovery mode was entered
    bool start_if_needed(const PlatformSharedResources, ModeHost *);
}