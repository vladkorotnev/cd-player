#include <esper-core/keypad.h>
#include <esper-core/consts.h>

static const char LOG_TAG[] = "KEYPAD";

namespace Platform {
    Keypad::Keypad(Core::ThreadSafeI2C * bus, uint8_t addr):
        _bus(bus),
        _addr(addr) {}

    bool Keypad::update(uint8_t * outVal) {
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
                }
                else {
                    last_value = ~wire->read();
                    if(outVal != nullptr) *outVal = last_value;
                    rslt = true;
                }
            }
        }

        _bus->release();
        return rslt;
    }

    uint8_t Keypad::get_value() {
        return last_value;
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

    Button::Button(Keypad * kp, uint8_t bitmask) {
        keypad = kp;
        mask = bitmask;
    }

    Button::State Button::get_state() {
        TickType_t now = xTaskGetTickCount();

        bool pressed = (keypad->get_value() & mask) != 0;
        if(pressed != cur_state) {
            cur_state = pressed;
            changed_at = now;
        }

        if(cur_state) {
            if(now - changed_at >= pdMS_TO_TICKS(500)) {
                click_flag = false;
                State rslt = hold_flag ? BTN_STATE_HOLD_CONTINUES : BTN_STATE_HELD;
                hold_flag = true;
                return rslt;
            } else {
                click_flag = true;
                return BTN_STATE_PRESSED;
            }
        } else {
            hold_flag = false;
            if(click_flag) {
                click_flag = false;
                return BTN_STATE_CLICKED;
            } else {
                return BTN_STATE_RELEASED;
            }
        }
    }
}