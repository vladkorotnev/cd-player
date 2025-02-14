#pragma once
#include <esper-gui/views/view.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace UI {

class BigSpinner: public View {
public:
    int interval_ms = 66;

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
        return View::needs_display();
    }

private:
    TickType_t last_tick = 0;
    int pos = 0;
};

}