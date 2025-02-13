#pragma once
#include <esper-gui/views/view.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace UI {

class BigSpinner: public View {
public:
    BigSpinner(EGRect f): View(f) {
        unsigned int bit_size = 2;
        auto rect = std::make_shared<FilledRect>(FilledRect());
        rect->frame = {{0, 0}, {bit_size, bit_size}}; // top left
        subviews.push_back(rect);

        rect = std::make_shared<FilledRect>(FilledRect());
        rect->frame = {{(int)(f.size.width / 2 - bit_size / 2), 0}, {bit_size, bit_size}}; // top middle
        rect->hidden = true;
        pos = 1;
        subviews.push_back(rect);

        rect = std::make_shared<FilledRect>(FilledRect());
        rect->frame = {{(int)(f.size.width - bit_size), 0}, {bit_size, bit_size}}; // top right
        subviews.push_back(rect);

        rect = std::make_shared<FilledRect>(FilledRect());
        rect->frame = {{(int)(f.size.width - bit_size), (int)(f.size.height / 2 - bit_size / 2)}, {bit_size, bit_size}}; // middle right
        subviews.push_back(rect);

        rect = std::make_shared<FilledRect>(FilledRect());
        rect->frame = {{(int)(f.size.width - bit_size), (int)(f.size.height - bit_size)}, {bit_size, bit_size}}; // bottom right
        subviews.push_back(rect);

        rect = std::make_shared<FilledRect>(FilledRect());
        rect->frame = {{(int)(f.size.width / 2 - bit_size / 2), (int)(f.size.height - bit_size)}, {bit_size, bit_size}}; // bottom middle
        subviews.push_back(rect);

        rect = std::make_shared<FilledRect>(FilledRect());
        rect->frame = {{0, (int)(f.size.height - bit_size)}, {bit_size, bit_size}}; // bottom left
        subviews.push_back(rect);

        rect = std::make_shared<FilledRect>(FilledRect());
        rect->frame = {{0, (int)(f.size.height / 2 - bit_size / 2)}, {bit_size, bit_size}}; // middle left
        subviews.push_back(rect);
    }

    bool needs_display() override {
        TickType_t now = xTaskGetTickCount();
        if(now - last_tick >= pdMS_TO_TICKS(interval_ms)) {
            subviews[pos]->hidden = false;
            pos = (pos + 1) % subviews.size();
            subviews[pos]->hidden = true;
            last_tick = now;
        }
        if(now - last_lfo_tick >= pdMS_TO_TICKS(33)) {
            if(lfo_dir) {
                if(interval_ms >= interval_ms_max) lfo_dir = false;
                else interval_ms += 5;
            }
            else {
                if(interval_ms <= interval_ms_min) lfo_dir = true;
                else interval_ms -= 5;
            }
            last_lfo_tick = now;
        }
        return View::needs_display();
    }

private:
    TickType_t last_tick = 0;
    TickType_t last_lfo_tick = 0;
    bool lfo_dir = false;
    int interval_ms_min = 75;
    int interval_ms_max = 125;
    int interval_ms = 125;
    int pos = 0;
};

}