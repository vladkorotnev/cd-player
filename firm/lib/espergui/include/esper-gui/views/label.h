#pragma once
#include "view.h"
#include <esper-gui/text.h>

namespace UI {
    class Label: public View {
    public:
        Label(EGRect frame, const Fonts::Font * font, std::string str = ""): View(frame), value{str}, fnt{font} {}

        void set_value(std::string str) {
            if(str != value) {
                value = str;
                set_needs_display();
            }
        }

        void render(EGGraphBuf * buf) override {
            Fonts::EGFont_put_string(fnt, value.c_str(), EGPointZero, buf);
            View::render(buf);
        }

    private:
        std::string value = "";
        const Fonts::Font* fnt = nullptr;
    };
}