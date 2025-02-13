#pragma once
#include "display.h"
#include <vector>
#include <memory>
#include <algorithm>

namespace Graphics {

typedef uint8_t* GraphBuf;

class View {
public:
    EGRect frame;
    bool hidden = false;
    std::vector<std::shared_ptr<View>> subviews;

    View(): frame {{0, 0}, {0, 0}}, subviews{} {}

    virtual void render(GraphBuf buffer) {
        // NB: child classes must call super impl or the performance will degrade!
        clear_needs_display();
    }

    bool needs_display() {
        return dirty || (was_hidden != hidden) || need_layout();
    }

    void clear_needs_display() {
        dirty = false;
        was_hidden = hidden;
        was_frame = frame;
        subview_count = subviews.size();
    }

    void set_needs_display() {
        dirty = true;
    }
    
private:
    bool dirty = true; // first draw is imminent
    bool was_hidden = false;
    EGRect was_frame = {{0, 0}, {0, 0}};
    int subview_count = 0;

    bool need_layout() {
        if(!EGRectEqual(was_frame, frame) || subview_count != subviews.size()) return true;

        return std::any_of(subviews.cbegin(), subviews.cend(), [](std::shared_ptr<View> v) { return v->need_layout(); });
    }
};

class Compositor {
public:
    Compositor(Hardware::DisplayDriver* driver, uint16_t width = 160, uint8_t height = 32) {
        framebuffer = {
            .data = (uint8_t*) calloc(width * (height / 8), 1),
            .size = { .width = width, .height = height }
        };
        display = driver;
    }

    void render(View& view);
private:
    void render_into_buffer(View&, std::vector<EGRect>*, const EGPoint);
    EGRect absolute_rect_to_tile_grid(const EGRect);
    Hardware::BackingBuffer framebuffer;
    Hardware::DisplayDriver* display;
};

}