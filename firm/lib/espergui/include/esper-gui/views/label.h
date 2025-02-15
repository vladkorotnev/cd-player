#pragma once
#include "view.h"
#include <esper-gui/text.h>

namespace UI {
    class Label: public View {
    public:
        Label(EGRect frame, int size, std::string str = ""): View(frame), value{str}, fontSize{size} {}

        void set_value(std::string str) {
            if(str != value) {
                value = str;
                set_needs_display();
            }
        }

        void render(EGGraphBuf * buf) override {
            Fonts::Font wildcard = Fonts::FallbackWildcard(fontSize);
            Fonts::EGFont_put_string(&wildcard, value.c_str(), EGPointZero, buf);
            View::render(buf);
        }

    private:
        std::string value = "";
        int fontSize = 8;
    };
}