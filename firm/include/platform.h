#pragma once
#include <esper-core/thread_safe_i2c.h>
#include <esper-core/keypad.h>
#include <esper-gui/compositing.h>

/// @brief Contains the resources shared across modes
struct PlatformSharedResources {
    Core::ThreadSafeI2C * i2c;
    Platform::Keypad * keypad;
};

const EGSize DISPLAY_SIZE = {160, 32};