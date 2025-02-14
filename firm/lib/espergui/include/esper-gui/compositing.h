#pragma once
#include "display.h"
#include "views/view.h"
#include <vector>

namespace Graphics {

class Compositor {
public:
    Compositor(Hardware::DisplayDriver* driver, uint16_t width = 160, uint8_t height = 32) {
        framebuffer = {
            .fmt = EG_FMT_NATIVE,
            .size = { .width = width, .height = height },
            .data = (uint8_t*) calloc(width * (height / 8), 1),
        };
        display = driver;
    }

    void render(UI::View& view);
private:
    void render_into_buffer(UI::View&, std::vector<EGRect>*, const EGPoint, bool);
    Hardware::BackingBuffer framebuffer;
    Hardware::DisplayDriver* display;
};

}