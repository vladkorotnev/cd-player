#pragma once
#include <freertos/semphr.h>

class Lock {
public:
    explicit Lock(SemaphoreHandle_t mutex): mutex(mutex) { xSemaphoreTake(mutex, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(mutex); }
private:
    SemaphoreHandle_t mutex;
};
