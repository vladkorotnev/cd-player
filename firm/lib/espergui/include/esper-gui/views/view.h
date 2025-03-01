#pragma once
#include <esp32-hal-log.h>
#include <esper-gui/graphics.h>
#include <vector>
#include <memory>
#include <algorithm>

namespace UI {

class View {
public:
    /// @brief Location of the view, relative to the parent, or the framebuffer if there is no parent
    EGRect frame;
    /// @brief Whether the view and all of its subviews should be not be rendered
    bool hidden = false;
    std::vector<std::shared_ptr<View>> subviews;

    View(EGRect f = EGRectZero): frame {f}, subviews{} {}
    virtual ~View() = default;

    /// @brief Render a representation of the view into the buffer. The buffer shall be of the size more than or equal to the view's `frame`.
    virtual void render(EGGraphBuf * buffer) {
        // NB: child classes must call super impl or the performance will degrade!
        clear_needs_display();
    }

    /// @brief Whether the view needs to be drawn again
    virtual bool needs_display() {
        return dirty || was_hidden != hidden || need_layout();
    }

    void clear_needs_display() {
        dirty = false;
        was_hidden = hidden;
        was_frame = frame;
        subview_count = subviews.size();
    }

    /// @brief Mark the view as needing to be drawn during the next composition pass
    void set_needs_display() {
        if(hidden) return;
        dirty = true;
    }
    
private:
    bool dirty = true; // first draw is imminent
    bool was_hidden = false;
    EGRect was_frame = {{0, 0}, {0, 0}};
    int subview_count = 0;

protected:
    /// @brief Whether the view or any of its children had changes to the geometry, requiring the whole view to be repainted 
    bool need_layout() {
        if(hidden) return false;
        if(!EGRectEqual(was_frame, frame) || subview_count != subviews.size()) {
            ESP_LOGW("Layout", "View(%p) needs a layout pass! Hidden=(%i -> %i) Frame=(%i %i %i %i -> %i %i %i %i) Subviews=(%i -> %i)", this, was_hidden, hidden, was_frame.origin.x, was_frame.origin.y, was_frame.size.width, was_frame.size.height, frame.origin.x, frame.origin.y, frame.size.width, frame.size.height, subview_count, subviews.size());
            return true;
        }

        return std::any_of(subviews.cbegin(), subviews.cend(), [](std::shared_ptr<View> v) { return v->need_layout(); });
    }
};

class FilledRect: public View {
public:
    FilledRect(): View() {}
    void render(EGGraphBuf * buffer) override {
        memset(buffer->data, 0xFF, frame.size.width*std::max(1, frame.size.height/8));
        View::render(buffer);
    }
};

}