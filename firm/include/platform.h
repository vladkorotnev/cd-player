#pragma once
#include <esper-core/platform.h>
#include <esper-gui/compositing.h>

/// @brief Contains the resources shared across modes
struct PlatformSharedResources {
    Core::ThreadSafeI2C * i2c;
    Platform::Keypad * keypad;
    Platform::AudioRouter * router;
};

const EGSize DISPLAY_SIZE = {160, 32};