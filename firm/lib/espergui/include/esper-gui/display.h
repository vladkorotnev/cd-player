#pragma once
#include "graphics.h"
#include <string.h>

namespace Graphics::Hardware {

enum Brightness: uint8_t {
    DISP_BRIGHTNESS_0 = 0,
    DISP_BRIGHTNESS_25,
    DISP_BRIGHTNESS_50,
    DISP_BRIGHTNESS_75,
    DISP_BRIGHTNESS_100
};

typedef EGGraphBuf BackingBuffer;

class DisplayDriver {
public:
    virtual void initialize() {}
    virtual void set_power(bool on) {}
    virtual void set_brightness(Brightness) {}

    /// NB: Reads from `buffer` are aligned to byte. E.g. writing (W=1 H=8) at (X=0 Y=4) will read from `buffer[0]` for the top half of the blitted bitmap and `buffer[1]` for the bottom half.
    virtual void transfer(const EGPoint address, const EGSize size, const BackingBuffer * buffer) {}
};

}