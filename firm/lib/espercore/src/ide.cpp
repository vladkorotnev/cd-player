#include <esper-core/ide.h>
#include <esper-core/consts.h>

static const char LOG_TAG[] = "IDE";

#define CTL_A0    (1 << 0)
#define CTL_A1    (1 << 1)
#define CTL_A2    (1 << 2)
#define CTL_CS1FX (1 << 3) // <- might be confusing but CS in this context selects the register block, not master/slave IDE drive
#define CTL_CS3FX (1 << 4)
#define CTL_RST   (1 << 5)
#define CTL_DIOW  (1 << 6)
#define CTL_DIOR  (1 << 7)
static const uint8_t CTL_MASK = (CTL_DIOW | CTL_DIOR | CTL_RST);


namespace Platform {

    IDEBus::IDEBus(Core::ThreadSafeI2C * i2c, uint8_t address_flags, uint8_t address_databus):
        _bus(i2c),
        _addr_flags(address_flags),
        _addr_databus(address_databus) {}

    data16 IDEBus::read(uint8_t reg) {
        ensure_sel_device();
        data16 rslt = { .value = 0xFFFF };

        // set data bus to input
        transact_out(_addr_databus, PCA_CFG_REGI_A, 0xFF);
        transact_out(_addr_databus, PCA_CFG_REGI_B, 0xFF);

        reg &= ~CTL_MASK;
        control_register &= CTL_MASK;
        control_register |= reg;
        control_register &= ~CTL_DIOR; // DIOR low
        transact_out(_addr_flags, PCA_DATA_OUT_REGI_B, control_register);

        rslt.low = transact_in(_addr_databus, PCA_DATA_IN_REGI_A);
        rslt.high = transact_in(_addr_databus, PCA_DATA_IN_REGI_B);
        
        control_register |= CTL_DIOR; // DIOR high
        transact_out(_addr_flags, PCA_DATA_OUT_REGI_B, control_register);

        ESP_LOGD(LOG_TAG, "read reg = 0x%02x, rslt low = 0x%02x, hi = 0x%02x", reg, rslt.low, rslt.high);

        return rslt;
    }

    void IDEBus::write(uint8_t reg, data16 data) { 
        ensure_sel_device();
        write_internal(reg, data);
    }

    void IDEBus::write_internal(uint8_t reg, data16 data) { 
        reg = reg & ~CTL_MASK; // don't allow changing DIOW/DIOR/RST from outside
        control_register &= CTL_MASK; // clear CS3FX, CS1FX, A2-A0
        control_register |= reg; // set CS3FX, CS1FX, A2-A0 from input argument

        // Set DIOW high
        control_register |= CTL_DIOW;
        transact_out(_addr_flags, PCA_DATA_OUT_REGI_B, control_register);

        ESP_LOGD(LOG_TAG, "write reg = 0x%02x, low = 0x%02x, hi = 0x%02x", reg, data.low, data.high);

        // enable data bus output
        transact_out(_addr_databus, PCA_CFG_REGI_A, 0x00);
        transact_out(_addr_databus, PCA_CFG_REGI_B, 0x00);

        // set data bus
        transact_out(_addr_databus, PCA_DATA_OUT_REGI_A, data.low);
        transact_out(_addr_databus, PCA_DATA_OUT_REGI_B, data.high);

        // DIOW low then high
        control_register &= ~CTL_DIOW;
        transact_out(_addr_flags, PCA_DATA_OUT_REGI_B, control_register);
        control_register |= CTL_DIOW;
        transact_out(_addr_flags, PCA_DATA_OUT_REGI_B, control_register);

        // return data bus to high-z
        transact_out(_addr_databus, PCA_CFG_REGI_A, 0xFF);
        transact_out(_addr_databus, PCA_CFG_REGI_B, 0xFF);
    }

    void IDEBus::reset() {
        ESP_LOGI(LOG_TAG, "Reset");

        transact_out(_addr_flags, PCA_CFG_REGI_B, 0x00); // all outputs
        transact_out(_addr_flags, PCA_DATA_OUT_REGI_B, ~CTL_RST);
        
        delay(40);

        control_register = 0xFF;
        transact_out(_addr_flags, PCA_DATA_OUT_REGI_B, control_register);

        delay(20);

        ESP_LOGV(LOG_TAG, "end of Reset");
    }

    void IDEBus::ensure_sel_device() {
        if(last_sel_device == active_device) return;

        ESP_LOGI(LOG_TAG, "Active device change from %i to %i", last_sel_device, active_device);
        IDE::DriveSelectRegister tmp = {{
            .lba_hi = 0,
            .slave_device = (active_device == DeviceNumber::IDE_SLAVE),
            .reserved0 = true, //<- according to http://oswiki.osask.jp/?ATA, old ATAPI devices might need these bits enabled 
            .enable_lba = false,
            .reserved1 = true,
        }};

        write_internal(IDE::Register::DriveSelect, {{ .low = tmp.value, .high = 0xFF }});
        delayMicroseconds(1); // https://wiki.osdev.org/ATA_PIO_Mode#400ns_delays

        last_sel_device = active_device;
    }

    void IDEBus::transact_out(uint8_t addr, uint8_t reg, uint8_t val) {
        if(!_bus->lock()) {
            ESP_LOGE(LOG_TAG, "Could not acquire bus");
            return;
        }

        auto wire = _bus->get();

        wire->beginTransmission(addr);
        wire->write(reg);
        wire->write(val);
        int err = wire->endTransmission();
        if(err) {
            ESP_LOGE(LOG_TAG, "Fail writing to 0x%02x::0x%02x, error = %i", addr, reg, err);
        }

        _bus->release();
    }

    uint8_t IDEBus::transact_in(uint8_t addr, uint8_t reg) {
        if(!_bus->lock()) {
            ESP_LOGE(LOG_TAG, "Could not acquire bus");
            return 0xFF;
        }

        auto wire = _bus->get();
        uint8_t rslt = 0xFF;

        wire->beginTransmission(addr);
        wire->write(reg);
        int err = wire->endTransmission();
        if(err) {
            ESP_LOGE(LOG_TAG, "Fail writing to 0x%02x::0x%02x, error = %i", addr, reg, err);
        } else {
            if(wire->requestFrom(addr, (uint8_t)1) != 1) {
                ESP_LOGE(LOG_TAG, "Fail reading from 0x%02x::0x%02x", addr, reg);
            } else {
                rslt = wire->read();
            }
        }

        _bus->release();
        return rslt;
    }
}