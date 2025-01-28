#pragma once
#include "thread_safe_i2c.h"

namespace Platform {
    /// @brief IDE Bus accessor
    class IDE {
    public:
        union data16 {
            struct { uint8_t low; uint8_t high; };
            uint16_t value;
        };

        /// @brief Create an IDE bus accessor
        /// @param i2c I2C Bus on which the PCA9555 expanders are located
        /// @param address_flags Address of the expander with the port B connected to the flags pins of the IDE bus
        /// @param address_databus Address of the expander with ports A and B connected to the 16 data bits of the IDE bus
        IDE(Core::ThreadSafeI2C * i2c, uint8_t address_flags = 0x20, uint8_t address_databus = 0x22);

        data16 read(uint8_t reg);
        void write(uint8_t reg, data16 data);
        void reset();

    private:
        Core::ThreadSafeI2C * _bus;
        uint8_t _addr_flags;
        uint8_t _addr_databus;
        uint8_t control_register = 0xF8;
        
        void transact_out(uint8_t addr, uint8_t reg, uint8_t val);
        uint8_t transact_in(uint8_t addr, uint8_t reg);
    };
}