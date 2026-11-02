#pragma once
#include "display.h"
#include "views/view.h"
#include <vector>

namespace Graphics {

class Compositor {
public:
    Compositor(Hardware::DisplayDriver* driver) {
        EGSize display_size = driver->get_size();
        framebuffer = {
            .fmt = EG_FMT_NATIVE,
            .size = display_size,
            .data = (uint8_t*) calloc(display_size.width * (display_size.height / 8), 1),
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