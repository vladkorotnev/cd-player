#pragma once
#include <esper-gui/graphics.h>
#include <vector>
#include <memory>
#include <algorithm>

namespace UI {

class View {
public:
    EGRect frame;
    bool hidden = false;
    std::vector<std::shared_ptr<View>> subviews;

    View(EGRect f = EGRectZero): frame {f}, subviews{} {}

    virtual void render(EGGraphBuf * buffer) {
        // NB: child classes must call super impl or the performance will degrade!
        clear_needs_display();
    }

    virtual bool needs_display() {
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

class FilledRect: public View {
public:
    FilledRect(): View() {}
    void render(EGGraphBuf * buffer) override {
        memset(buffer->data, 0xFF, frame.size.width*std::max(1u, frame.size.height/8));
        View::render(buffer);
    }
};

}