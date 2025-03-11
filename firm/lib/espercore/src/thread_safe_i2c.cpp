#include <esper-core/thread_safe_i2c.h>
#include <freertos/FreeRTOS.h>
#include <esp32-hal-log.h>

static const char LOG_TAG[] = "TSI2C";

namespace Core {
    ThreadSafeI2C::ThreadSafeI2C(TwoWire* bus) {
        _bus = bus;
        _semaphore = xSemaphoreCreateBinary();
        xSemaphoreGive(_semaphore);
    }

    bool ThreadSafeI2C::lock(TickType_t maxWait) {
        if(!xSemaphoreTake(_semaphore, maxWait)) {
            ESP_LOGW(LOG_TAG, "Time out while waiting on I2C semaphore");
            return false;
        }
        return true;
    }

    void ThreadSafeI2C::release() {
        xSemaphoreGive(_semaphore);
    }

    void ThreadSafeI2C::log_all_devices() {
        lock();

        bool found = false;
        uint8_t error;

        for(uint8_t address = 1; address < 127; address++) {
            Wire.beginTransmission(address);
            error = Wire.endTransmission();
            if(error == 0) {
                ESP_LOGI(LOG_TAG, "I2C Device @ 0x%02x", address);
                found = true;
            }
            else if(error == 4) {
                ESP_LOGW(LOG_TAG, "Error when testing address 0x%02x", address);
            }
        }

        if(!found) ESP_LOGW(LOG_TAG, "No I2C devices found!");

        release();
    }
}