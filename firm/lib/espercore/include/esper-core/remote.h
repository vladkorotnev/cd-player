#pragma once
#include <esp32-hal-gpio.h>

namespace Platform {
    class Remote {
    public:
        Remote(gpio_num_t pin = GPIO_NUM_34);

        void update();
    };
};