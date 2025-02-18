#pragma once
#include <esper-gui/display.h>
#include <esp32-hal-gpio.h>

namespace Graphics::Hardware {

class FutabaGP1232ADriver: public DisplayDriver {
public:
    const EGSize DISPLAY_SIZE = {160, 32};
    FutabaGP1232ADriver(uart_port_t uart_num = 1, gpio_num_t rx_pin = GPIO_NUM_23, gpio_num_t tx_pin = GPIO_NUM_22, int baud = 120000);

    /// @brief Enable anti tearing mode using multiple DSA pages to avoid visible tearing when updating large chunks of the display
    bool anti_tearing = false;

    void initialize() override;
    void set_power(bool on) override;
    void set_brightness(Brightness) override;
    void transfer(const std::vector<EGRect> changes, const BackingBuffer * buffer) override;
private:
    void transfer(const EGPoint address, const EGSize size, const BackingBuffer * buffer);
    const int DSA_FIRST = 0;
    const int DSA_SECOND = 160;
    int visible_dsa = 0;
    void flip_pages();
    uart_port_t _port;
};

}