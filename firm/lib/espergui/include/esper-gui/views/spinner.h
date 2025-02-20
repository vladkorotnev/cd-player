#pragma once
#include <esper-gui/views/view.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace UI {

class TinySpinner: public View {
public:
    int interval_ms = 125;
    TinySpinner(EGRect f): View(f) {}

    bool needs_display() override {
        TickType_t now = xTaskGetTickCount();
        if(now - last_tick >= pdMS_TO_TICKS(interval_ms) && !hidden) {
            pos = (pos + 1) % 8;
            last_tick = now;
            return true;
        }
        return View::needs_display();
    }

    void render(EGGraphBuf * buf) override {
        const EGPoint offsets[] = {
            {0, 0},
            {1, 0},
            {2, 0},
            {2, 1},
            {2, 2},
            {1, 2},
            {0, 2},
            {0, 1}
        };
        int bit_size = frame.size.height / 3;
        for(int i = 0; i < 8; i++) {
            EGPoint p = offsets[i];
            p.x *= 2*bit_size;
            p.y *= 2*bit_size;
            EGDrawRect(buf, {p, {bit_size, bit_size}}, true, (i != pos));
        }
        View::render(buf);
    }

private:
    TickType_t last_tick = 0;
    int pos = 0;
};

}