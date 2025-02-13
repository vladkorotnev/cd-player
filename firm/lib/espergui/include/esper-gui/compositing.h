#pragma once
#include "display.h"
#include "views/view.h"
#include <vector>

namespace Graphics {

class Compositor {
public:
    Compositor(Hardware::DisplayDriver* driver, uint16_t width = 160, uint8_t height = 32) {
        framebuffer = {
            .data = (uint8_t*) calloc(width * (height / 8), 1),
            .size = { .width = width, .height = height }
        };
        display = driver;
    }

    void render(UI::View& view);
private:
    void render_into_buffer(UI::View&, std::vector<EGRect>*, const EGPoint, bool);
    EGRect absolute_rect_to_tile_grid(const EGRect);
    Hardware::BackingBuffer framebuffer;
    Hardware::DisplayDriver* display;
};

}