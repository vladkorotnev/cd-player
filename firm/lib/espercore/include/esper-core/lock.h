#pragma once
#include <freertos/semphr.h>

class [[nodiscard]] Lock {
public:
    explicit Lock(SemaphoreHandle_t mutex): mutex(mutex) { xSemaphoreTake(mutex, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(mutex); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
private:
    SemaphoreHandle_t mutex;
};
