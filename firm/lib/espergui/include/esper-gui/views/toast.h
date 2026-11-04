#pragma once
#include "label.h"
#include <esper-gui/text.h>
#include <functional>
#include <utility>

namespace UI {
    class Toast: public View {
    public:
        explicit Toast(std::function<void()> dismissCallback = {}):
            View(EGRectZero),
            dismissCallback(std::move(dismissCallback)) {
            label = std::make_shared<Label>(EGRectZero, Fonts::FallbackWildcard8px, Label::Alignment::Center);
            subviews.push_back(label);
            hidden = true;
        }

        void show(const std::string& text, const Fonts::Font * font, const TickType_t duration, const EGSize clientRect) {
            label->set_font(font);
            label->set_value(text);
            label->frame = {{2,2}, label->str_size};
            frame = {{clientRect.width / 2 - label->str_size.width / 2 - 2, clientRect.height/2 - label->str_size.height/2 - 2}, {label->str_size.width + 4, label->str_size.height + 4}};
            showDuration = duration;
            shownTimestamp = xTaskGetTickCount();
            isShown = false;
            hidden = false;
            set_needs_display();
        }

        void render(EGGraphBuf * b) override {
            View::render(b);
            EGDrawRect(b, {EGPointZero, frame.size});
            if (!isShown) {
                shownTimestamp = xTaskGetTickCount();
                isShown = true;
            }
        }

        bool needs_display() override {
            if (isShown) {
                TickType_t now = xTaskGetTickCount();
                if (now - shownTimestamp >= showDuration) {
                    hidden = true;
                    isShown = false;
                    if (dismissCallback) dismissCallback();
                }
            }
            return View::needs_display();
        }

    private:
        TickType_t shownTimestamp = 0;
        TickType_t showDuration = 0;
        bool isShown = false;
        std::function<void()> dismissCallback;
        std::shared_ptr<Label> label;
    };
};
