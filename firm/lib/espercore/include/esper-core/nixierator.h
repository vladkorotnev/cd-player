#pragma once
#include "thread_safe_i2c.h"

namespace Platform {
    class Nixierator {
    public:
        bool leading_zero = true;
        bool left_align = false;
        bool off_on_zero = true;

        Nixierator(Core::ThreadSafeI2C * i2c, uint8_t address = 0x38) {
            bus = i2c;
            addr = address;
        }

        void set_visible(bool visible) {
            set_value_internal(visible ? cur_value_bcd : 0xFF);
            is_visible = visible;
        }

        void set_value(uint8_t value) {
            value = value % 100;
            if(value == 0 && off_on_zero) {
                cur_value_bcd = 0xFF;
            } else {
                cur_value_bcd = ((value / 10) << 4) | (value % 10);
                if(value < 10) {
                    if(left_align) {
                        cur_value_bcd <<= 4;
                        cur_value_bcd |= 0x0F;
                    }
                    else if(!leading_zero) cur_value_bcd |= 0xF0;
                }
            }
            if(is_visible) set_value_internal(cur_value_bcd);
        }

    private:
        const char * LOG_TAG = "NIX";
        Core::ThreadSafeI2C * bus;
        uint8_t cur_value_bcd = 0xFF;
        uint8_t addr = 0;
        bool is_visible = true;
        void set_value_internal(uint8_t value) {
            if(!bus->lock()) {
                ESP_LOGE(LOG_TAG, "Failed to acquire bus");
                return;
            }

            auto wire = bus->get();
            wire->beginTransmission(addr);
            wire->write(value);
            wire->endTransmission(true);
            bus->release();
        }
    };
}