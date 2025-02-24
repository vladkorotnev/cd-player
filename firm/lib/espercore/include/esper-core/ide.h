#pragma once
#include "thread_safe_i2c.h"

union data16 {
    struct { uint8_t low; uint8_t high; };
    uint16_t value;
};

namespace IDE {
    enum Register: uint8_t {
        Data = 0xF0,
        Error = 0xF1,
        Feature = Error,
        SectorCount = 0xF2,
        SectorNumber = 0xF3,
        CylinderLow = 0xF4,
        CylinderHigh = 0xF5,
        DriveSelect = 0xF6,
        Command = 0xF7,
        Status = Command,
        AlternateStatus = 0xEE,
        DeviceControl = AlternateStatus
    };

    union DriveSelectRegister {
        struct {
            uint8_t lba_hi: 4;
            bool slave_device: 1; 
            bool reserved0: 1;
            bool enable_lba: 1; // LBA if on, otherwise CHS
            bool reserved1: 1;
        } bits;
        uint8_t value;
    };

    enum DeviceNumber {
        IDE_MASTER,
        IDE_SLAVE
    };
};

namespace Platform {    
    /// @brief IDE Bus accessor. Not thread safe!
    class IDEBus {
    public:
        using DeviceNumber = IDE::DeviceNumber;
        /// @brief Create an IDE bus accessor
        /// @param i2c I2C Bus on which the PCA9555 expanders are located
        /// @param address_flags Address of the expander with the port B connected to the flags pins of the IDE bus
        /// @param address_databus Address of the expander with ports A and B connected to the 16 data bits of the IDE bus
        IDEBus(Core::ThreadSafeI2C * i2c, uint8_t address_flags = 0x20, uint8_t address_databus = 0x22);

        DeviceNumber active_device = DeviceNumber::IDE_MASTER; // <- dont seem to be able to address other device but still

        data16 read(uint8_t reg);
        void read_bulk(uint8_t reg, void* buf, size_t size);
        void write(uint8_t reg, data16 data);
        void reset();

    private:
        Core::ThreadSafeI2C * _bus;
        uint8_t _addr_flags;
        uint8_t _addr_databus;
        uint8_t control_register = 0xF8;
        uint8_t last_sel_device = 0xFF;
        
        void transact_out(uint8_t addr, uint8_t reg, uint8_t val);
        uint8_t transact_in(uint8_t addr, uint8_t reg);

        void transact_out_locked(uint8_t addr, uint8_t reg, uint8_t val);
        uint8_t transact_in_locked(uint8_t addr, uint8_t reg);
        uint8_t transact_in_continuation_locked(uint8_t addr);

        void ensure_sel_device();
        void write_internal(uint8_t reg, data16 data);
    };
}