#pragma once
#include "view.h"
#include <esper-gui/text.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>

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

        // Positive rates are pixels per 33 ms tick. Negative rates are the
        // number of ticks per pixel, allowing speeds below one pixel per tick.
        static bool is_valid_scroll_speed(int rate) {
            return rate >= -4 && rate <= 4 && rate != 0;
        }

        static void set_scroll_speed(int rate) {
            scroll_rate.store(is_valid_scroll_speed(rate) ? rate : 2);
        }

        Alignment alignment = Alignment::Left;

        Label(EGRect frame, const Fonts::Font * font, Alignment align, std::string str = ""): View(frame), fnt{font}, alignment{align}, valueSemaphore(xSemaphoreCreateBinary()) {
            assert(valueSemaphore != NULL);
            xSemaphoreGive(valueSemaphore);
            set_value(str);
        }

        void set_value(std::string str) {
            if(str != value) {
                xSemaphoreTake(valueSemaphore, portMAX_DELAY);
                value = str;
                recalculate();
                xSemaphoreGive(valueSemaphore);
            }
        }

        void set_font(const Fonts::Font * font) {
            xSemaphoreTake(valueSemaphore, portMAX_DELAY);
            fnt = font;
            recalculate();
            xSemaphoreGive(valueSemaphore);
        }

        void render(EGGraphBuf * buf) override {
            if(!xSemaphoreTake(valueSemaphore, pdMS_TO_TICKS(33))) {
                ESP_LOGW("Render", "Label(%p) locked for too long during render pass!", this);
                set_needs_display();
                return; // render at next earliest convenience, but don't stall the blitter
            }

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
            xSemaphoreGive(valueSemaphore);
        }

        bool needs_display() override {
            TickType_t now = xTaskGetTickCount();
            if(will_scroll() && (now - last_scroll_tick >= pdMS_TO_TICKS(33))) {
                last_scroll_tick = now;
                bool is_follower = scroll_synchro != nullptr && (*scroll_synchro)->will_scroll() && (*scroll_synchro)->str_size.width > str_size.width && !(*scroll_synchro)->hidden;
                if((scroll_holdoff == 0 && (!is_follower || (is_follower && (*scroll_synchro)->scroll_holdoff == 0))) || scroll_offset != 0) {
                    const int rate = scroll_rate.load();
                    const int ticks_per_pixel = rate < 0 ? -rate : 1;
                    if(++scroll_subtick < ticks_per_pixel) return View::needs_display();
                    scroll_subtick = 0;
                    const int next_offset = scroll_offset + (rate < 0 ? 1 : rate);
                    scroll_offset = scroll_offset < 0 && next_offset >= 0 ? 0 : next_offset;
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
        void recalculate() {
            scroll_offset = 0;
            str_size = Fonts::EGFont_measure_string(fnt, value.c_str());
            set_needs_display();
            last_scroll_tick = xTaskGetTickCount();
            scroll_subtick = 0;
        }
    private:
        SemaphoreHandle_t valueSemaphore;
        std::string value = "";
        const Fonts::Font* fnt = nullptr;
        int scroll_offset = 0;
        int scroll_holdoff = 60;
        int scroll_subtick = 0;
        TickType_t last_scroll_tick = 0;
        std::shared_ptr<UI::Label>* scroll_synchro = nullptr; // Y U NO HAVE Option<>??
        inline static std::atomic<int> scroll_rate{2};
    };
}
