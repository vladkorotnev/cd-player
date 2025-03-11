#pragma once
#include <Wire.h>

namespace Core {
    class ThreadSafeI2C {
    public:
        ThreadSafeI2C(TwoWire * bus);
        
        bool lock(TickType_t maxWait = portMAX_DELAY);
        void release();

        TwoWire * get() { return _bus; }
        void log_all_devices();
    private:
        TwoWire * _bus;
        SemaphoreHandle_t _semaphore;
    };
}