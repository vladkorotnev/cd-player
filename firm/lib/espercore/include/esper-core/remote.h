#pragma once
#include <esp32-hal-gpio.h>
#include <Freenove_IR_Lib_for_ESP32.h>

namespace Platform {
    class Remote {
    public:
        Remote(gpio_num_t pin = GPIO_NUM_34);

        void update();

    private:
        Freenove_ESP32_IR_Recv receiver;
    };
};