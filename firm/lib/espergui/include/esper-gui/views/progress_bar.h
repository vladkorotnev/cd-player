#pragma once
#include "view.h"

namespace UI {
    class ProgressBar: public View {
    public:
        int minimum = 0;
        int maximum = 100;
        int value = 50;
        bool filled = true;
        ProgressBar(EGRect frame): View(frame) {
        }

        bool needs_display() override {
            int new_width = calc_pix_width();
            if(new_width != pix_width) {
                pix_width = new_width;
                return true;
            }
            return View::needs_display();
        }

        void render(EGGraphBuf * buf) override {
            // draw a frame around the bar
            EGRect outer_frame = { EGPointZero, frame.size };
            EGDrawRect(buf, outer_frame);

            EGRect indicator_base = EGRectInset(outer_frame, 2, 2);
            indicator_base.size.width = std::min(indicator_base.size.width - 1, pix_width);

            if(filled) {
                EGDrawRect(buf, indicator_base, true);
            } else {
                EGDrawLine(buf, {indicator_base.origin.x + indicator_base.size.width, 1}, {indicator_base.origin.x + indicator_base.size.width, frame.size.height - 2});
            }
            View::render(buf);
        }
    private:
        int pix_width = 0;

        int calc_pix_width() {
            if(value <= minimum) return 0;
            if(value >= maximum) return frame.size.width - 2;
            return (((frame.size.width - 2) * (value - minimum)) / (maximum - minimum)) + ((((frame.size.width - 2) * (value - minimum)) / (maximum - minimum)) != 0);
        }
    };
}