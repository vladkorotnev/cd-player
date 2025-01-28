#include <esper-core/keypad.h>
#include <esper-core/consts.h>

static const char LOG_TAG[] = "KEYPAD";

namespace Platform {
    Keypad::Keypad(Core::ThreadSafeI2C * bus, uint8_t addr):
        _bus(bus),
        _addr(addr) {}

    bool Keypad::read(uint8_t * outVal) {
        if(outVal == nullptr) {
            ESP_LOGE(LOG_TAG, "outVal cannot be null");
            return false;
        }

        if(!_bus->lock()) {
            return false;
        }

        bool rslt = false;

        if(setup_locked()) {
            auto wire = _bus->get();
            wire->beginTransmission(_addr);
            wire->write(PCA_DATA_IN_REGI_A);
            int err = wire->endTransmission();
            if(err != 0) {
                ESP_LOGE(LOG_TAG, "Error %i when selecting inPort", err);
            } else {
                if(wire->requestFrom(_addr, (uint8_t)1) != 1) {
                    ESP_LOGE(LOG_TAG, "Error when reading inPort");
                    return false;
                }

                *outVal = ~wire->read();
                rslt = true;
            }
        }

        _bus->release();
        return rslt;
    }

    bool Keypad::setup_locked() {
        if(did_setup) return true;

        auto wire = _bus->get();
        wire->beginTransmission(_addr);
        wire->write(PCA_CFG_REGI_A);
        wire->write(0xFF); // all 8 bits are inputs
        int err = wire->endTransmission();
        if(err == 0) {
            did_setup = true;
            return true;
        }

        ESP_LOGE(LOG_TAG, "Error %i when configuring port", err);
        return false;
    }
}