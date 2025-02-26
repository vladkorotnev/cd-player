#pragma once
#include "view.h"
#include <esper-gui/text.h>
#include <freertos/semphr.h>

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
            if(will_scroll()) {
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
            if(frame.size.height > fnt->size.height) {
                origin.y = frame.size.height / 2 - fnt->size.height / 2;
            }
            Fonts::EGFont_put_string(fnt, value.c_str(), origin, buf);
            View::render(buf);
        }

        bool needs_display() override {
            TickType_t now = xTaskGetTickCount();
            if(will_scroll() && (now - last_scroll_tick >= pdMS_TO_TICKS(33))) {
                last_scroll_tick = now;
                bool is_follower = scroll_synchro != nullptr && (*scroll_synchro)->will_scroll() && (*scroll_synchro)->str_size.width > str_size.width;
                if((scroll_holdoff == 0 && (!is_follower || (is_follower && (*scroll_synchro)->scroll_holdoff == 0))) || scroll_offset != 0) {
                    scroll_offset += 2;
                    if(scroll_offset >= str_size.width + 16) {
                        scroll_offset = -frame.size.width;
                    }
                    if(scroll_offset == 0) {
                        scroll_holdoff = 60;
                    }
                    set_needs_display();
                } else if (!is_follower) {
                    scroll_holdoff--;
                } else if  (is_follower && (*scroll_synchro)->scroll_offset == 0) {
                    scroll_holdoff = 0;
                }
            }
            return View::needs_display();
        }

        void synchronize_scrolling_to(std::shared_ptr<UI::Label>* other) {
            scroll_synchro = other;
        }

    protected:
        bool will_scroll() { return auto_scroll && str_size.width > frame.size.width; }
    private:
        std::string value = "";
        const Fonts::Font* fnt = nullptr;
        int scroll_offset = 0;
        int scroll_holdoff = 60;
        TickType_t last_scroll_tick = 0;
        std::shared_ptr<UI::Label>* scroll_synchro = nullptr; // Y U NO HAVE Option<>??
    };
}