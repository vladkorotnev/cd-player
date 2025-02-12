#pragma once
#include "display.h"
#include <esp32-hal-gpio.h>

namespace Graphics::Hardware {

class FutabaGP1232ADriver: public DisplayDriver {
public:
    FutabaGP1232ADriver(uart_port_t uart_num = 1, gpio_num_t rx_pin = GPIO_NUM_23, gpio_num_t tx_pin = GPIO_NUM_22, int baud = 115200);
    void initialize() override;
    void set_power(bool on) override;
    void set_brightness(Brightness) override;
    void blit(const EGPoint address, const EGSize size, const BackingBuffer * buffer) override;
private:
    uart_port_t _port;
};

}