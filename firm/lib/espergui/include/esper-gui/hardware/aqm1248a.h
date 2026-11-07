#pragma once

#include <esper-gui/display.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace Graphics::Hardware {
#if (BCONF_DISPLAY_TYPE == DISP_TYPE_AQM1248A_SPI)
/// Driver for the 128x48 AQM1248A LCD module (ST7565R controller) in 4-wire SPI mode.
class AQM1248ADriver: public DisplayDriver {
public:
    AQM1248ADriver(
        spi_host_device_t spi_host,
        gpio_num_t sclk_pin,
        gpio_num_t mosi_pin,
        gpio_num_t cs_pin,
        gpio_num_t rs_pin,
        uint32_t frequency = 10000000U
    );
    ~AQM1248ADriver() override;

    void initialize() override;
    void set_option(DriverOption opt, bool on) override;
    void set_brightness(Brightness brightness) override;
    void transfer(const std::vector<EGRect> changes, const BackingBuffer * buffer) override;
    const EGSize& get_size() override { return DISPLAY_SIZE; }

private:
    static constexpr EGSize DISPLAY_SIZE = {128, 48};
    static constexpr int PAGE_HEIGHT = 8;
    static constexpr int VISIBLE_PAGES = DISPLAY_SIZE.height / PAGE_HEIGHT;
    static constexpr int CONTROLLER_PAGES = 8;
    static constexpr int CONTROLLER_COLUMNS = 132;
    static constexpr uint8_t DEFAULT_CONTRAST = 0x1C;

    static uint8_t reverse_bits(uint8_t value);
    void write_bytes(bool data, const uint8_t * bytes, size_t count);
    void write_commands(const uint8_t * commands, size_t count);
    void set_address(uint8_t page, uint8_t column);
    void clear_controller_ram();

    spi_host_device_t spi_host;
    spi_device_handle_t spi_device = nullptr;
    bool owns_spi_bus = false;
    gpio_num_t sclk_pin;
    gpio_num_t mosi_pin;
    gpio_num_t cs_pin;
    gpio_num_t rs_pin;
    uint32_t frequency;
    SemaphoreHandle_t mutex;
};
#endif
}
