#pragma once
#include "graphics.h"
#include <vector>
#include <memory>


namespace Graphics {

class View {
public:
    EGRect frame;
    std::vector<std::shared_ptr<View>> subviews;
    virtual void render(const uint8_t * buffer) {
        // NB: child classes must call super impl or the performance will degrade!
        dirty = false;
    }
protected:
    View(): frame {{0, 0}, {0, 0}}, subviews{} {}
    void set_needs_display() {
        dirty = true;
    }
    bool needs_display() {
        return dirty;
    }
private:
    bool dirty = false;
};

}