#pragma once
#include "thread_safe_i2c.h"

namespace Platform {
    /// @brief 8-bit keypad port located on a PCA9555D normally at address 0x20 (as of Rev1 of ESPer PCB)
    class Keypad {
    public:
        Keypad(Core::ThreadSafeI2C *, uint8_t address = 0x20);
        bool read(uint8_t * rslt);

    private:
        Core::ThreadSafeI2C * _bus;
        uint8_t _addr;
        bool did_setup = false;
        bool setup_locked();
    };
}