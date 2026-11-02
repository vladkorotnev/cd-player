#pragma once
#include <esper-core/platform.h>
#include <esper-cdp/atapi.h>
#include <esper-gui/compositing.h>

/// @brief Contains the resources shared across modes
struct PlatformSharedResources {
    Platform::Keypad * keypad;
    Platform::Remote * remote;
    Platform::AudioRouter * router;
    Graphics::Hardware::DisplayDriver * display;
    Platform::IDEBus * ide;
    ATAPI::Device * cdrom;
};
