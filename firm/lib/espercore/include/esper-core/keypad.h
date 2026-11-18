#pragma once
#include "thread_safe_i2c.h"

namespace Platform {
    class Keypad {
    public:
        virtual uint8_t get_value() = 0;
        virtual bool update(uint8_t * result = nullptr) = 0;
    protected:
        explicit Keypad() {}
    };

    class NullKeypad: public Keypad {
    public:
        NullKeypad(): Keypad() {}

        uint8_t get_value() override { return 0; }
        bool update(uint8_t * rslt = nullptr) override { 
            *rslt = 0;
            return true;
        }
    };

    /// @brief 8-bit keypad port located on a PCA9555D normally at address 0x20 (as of Rev1 of ESPer PCB)
    class I2CKeypad: public Keypad {
    public:
        I2CKeypad(Core::ThreadSafeI2C *, uint8_t address = 0x20);

        uint8_t get_value() override;
        bool update(uint8_t * rslt = nullptr) override;

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