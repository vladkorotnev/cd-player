#pragma once
#include <esper-gui/display.h>
#include <esp32-hal-gpio.h>

namespace Graphics::Hardware {
#if (BCONF_DISPLAY_TYPE == DISP_TYPE_FUTABA_GP1232A)
class FutabaGP1232ADriver: public DisplayDriver {
public:
    FutabaGP1232ADriver(uart_port_t uart_num, gpio_num_t rx_pin, gpio_num_t tx_pin, int baud);
    ~FutabaGP1232ADriver() {
        vSemaphoreDelete(_portSema);
        vSemaphoreDelete(_frameSema);
    }

    void initialize() override;
    void set_option(DriverOption opt, bool on) override;
    void set_brightness(Brightness) override;
    void transfer(const std::vector<EGRect> changes, const BackingBuffer * buffer) override;
    const EGSize& get_size() override { return DISPLAY_SIZE; }
private:
    bool anti_tearing = true;
    void set_power(bool on);
    void set_anti_tearing(bool on);
    void set_visible_dsa(int dsa);
    void transfer(const EGPoint address, const EGSize size, const BackingBuffer * buffer, int dsa);
    bool page_differs(int page, const BackingBuffer * buffer) const;
    void update_page(int page, const BackingBuffer * buffer);
    const int DSA_FIRST = 0;
    const int DSA_SECOND = 160;
    int visible_dsa = 0;
    void flip_pages();
    static constexpr EGSize DISPLAY_SIZE = {160, 32};
    static constexpr int PAGE_HEIGHT = 8;
    static constexpr int FRAME_BYTES = DISPLAY_SIZE.width * DISPLAY_SIZE.height / PAGE_HEIGHT;
    uint8_t page_shadow[2][FRAME_BYTES] = {};
    bool page_valid[2] = {false, false};
    uart_port_t _port;
    SemaphoreHandle_t _portSema;
    SemaphoreHandle_t _frameSema;
};
#endif
}
