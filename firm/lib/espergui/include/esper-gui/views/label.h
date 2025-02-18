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

        bool auto_scroll = false;
        EGSize str_size = EGSizeZero;

        Alignment alignment = Alignment::Left;

        Label(EGRect frame, const Fonts::Font * font, Alignment align, std::string str = ""): View(frame), fnt{font}, alignment{align} {
            set_value(str);
        }

        void set_value(std::string str) {
            if(str != value) {
                value = str;
                str_size = Fonts::EGFont_measure_string(fnt, value.c_str());
                set_needs_display();
                last_scroll_tick = xTaskGetTickCount();
                scroll_offset = 0;
            }
        }

        void render(EGGraphBuf * buf) override {
            EGPoint origin = EGPointZero;
            if(auto_scroll && str_size.width > frame.size.width) {
                origin.x = -scroll_offset;
            } else {
                if(alignment != Alignment::Left) {
                    if(alignment == Alignment::Center) {
                        origin.x = buf->size.width / 2 - str_size.width / 2;
                    } else {
                        origin.x = buf->size.width - str_size.width;
                    }
                }
            }
            Fonts::EGFont_put_string(fnt, value.c_str(), origin, buf);
            View::render(buf);
        }

        bool needs_display() override {
            TickType_t now = xTaskGetTickCount();
            if(auto_scroll && str_size.width > frame.size.width && (now - last_scroll_tick >= pdMS_TO_TICKS(66))) {
                last_scroll_tick = now;

                if(scroll_holdoff == 0 || scroll_offset != 0) {
                    scroll_offset += 4;
                    scroll_holdoff = 60;
                    if(scroll_offset >= str_size.width + 16) {
                        scroll_offset = -frame.size.width;
                    }
                    return true;
                } else {
                    scroll_holdoff--;
                }
            }
            return View::needs_display();
        }

    private:
        std::string value = "";
        const Fonts::Font* fnt = nullptr;
        int scroll_offset = 0;
        int scroll_holdoff = 60;
        TickType_t last_scroll_tick = 0;
    };
}