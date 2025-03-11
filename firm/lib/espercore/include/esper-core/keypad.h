#pragma once
#include "thread_safe_i2c.h"

namespace Platform {

    /// @brief 8-bit keypad port located on a PCA9555D normally at address 0x20 (as of Rev1 of ESPer PCB)
    class Keypad {
    public:
        Keypad(Core::ThreadSafeI2C *, uint8_t address = 0x20);

        uint8_t get_value();
        bool update(uint8_t * rslt = nullptr);

    private:
        Core::ThreadSafeI2C * _bus;
        uint8_t _addr;
        bool did_setup = false;
        bool setup_locked();
        uint8_t last_value = 0x0;
    };

    class Button {
    public:
        enum State {
            BTN_STATE_RELEASED,
            BTN_STATE_PRESSED,
            BTN_STATE_CLICKED,
            BTN_STATE_HELD,
            BTN_STATE_HOLD_CONTINUES
        };
        Button(Keypad *, uint8_t bitmask);

        State get_state();
        
        bool is_clicked() { return get_state() == BTN_STATE_CLICKED; }
        bool is_held() { return get_state() == BTN_STATE_HELD; }
        bool is_held_continuously() {
            auto const sts = get_state();
            return sts == BTN_STATE_HELD || sts == BTN_STATE_HOLD_CONTINUES; 
        }
        bool is_pressed() { 
            auto const sts = get_state();
            return sts == BTN_STATE_PRESSED || sts == BTN_STATE_HELD || sts == BTN_STATE_HOLD_CONTINUES; 
        }
        bool is_released() { return get_state() == BTN_STATE_RELEASED; }
    private:
        Keypad * keypad;
        TickType_t changed_at = 0;
        uint8_t mask = 0;
        bool cur_state = false;
        bool click_flag = false;
        bool hold_flag = false;
    };
}