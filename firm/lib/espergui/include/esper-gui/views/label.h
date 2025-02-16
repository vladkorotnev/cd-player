#pragma once
#include "view.h"
#include <esper-gui/text.h>

namespace UI {
    class Label: public View {
    public:
        enum class Alignment {
            Left,
            Center,
            Right
        };

        Alignment alignment = Alignment::Left;

        Label(EGRect frame, const Fonts::Font * font, Alignment align, std::string str = ""): View(frame), fnt{font}, alignment{align} {
            set_value(str);
        }

        void set_value(std::string str) {
            if(str != value) {
                value = str;
                str_size = Fonts::EGFont_measure_string(fnt, value.c_str());
                set_needs_display();
            }
        }

        void render(EGGraphBuf * buf) override {
            EGPoint origin = EGPointZero;
            if(alignment != Alignment::Left) {
                if(alignment == Alignment::Center) {
                    origin.x = buf->size.width / 2 - str_size.width / 2;
                } else {
                    origin.x = buf->size.width - str_size.width;
                }
            }
            Fonts::EGFont_put_string(fnt, value.c_str(), origin, buf);
            View::render(buf);
        }

    private:
        std::string value = "";
        EGSize str_size = EGSizeZero;
        const Fonts::Font* fnt = nullptr;
    };
}