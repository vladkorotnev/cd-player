#if (BCONF_DISPLAY_TYPE == DISP_TYPE_AQM1248A_SPI)

#include <esper-gui/hardware/aqm1248a.h>

#include <algorithm>
#include <cstring>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/task.h>

static const char LOG_TAG[] = "AQM1248A";

namespace Graphics::Hardware {

AQM1248ADriver::AQM1248ADriver(
    spi_host_device_t spi_host,
    gpio_num_t sclk_pin,
    gpio_num_t mosi_pin,
    gpio_num_t cs_pin,
    gpio_num_t rs_pin,
    uint32_t frequency
):
    spi_host(spi_host),
    sclk_pin(sclk_pin),
    mosi_pin(mosi_pin),
    cs_pin(cs_pin),
    rs_pin(rs_pin),
    frequency(frequency),
    mutex(xSemaphoreCreateMutex()) {}

AQM1248ADriver::~AQM1248ADriver() {
    if (spi_device != nullptr) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(spi_bus_remove_device(spi_device));
    }
    if (owns_spi_bus) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(spi_bus_free(spi_host));
    }
    if (mutex != nullptr) vSemaphoreDelete(mutex);
}

void AQM1248ADriver::initialize() {
    const gpio_config_t rs_config = {
        .pin_bit_mask = 1ULL << rs_pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&rs_config));
    ESP_ERROR_CHECK(gpio_set_level(rs_pin, 0));

    const spi_bus_config_t bus_config = {
        .mosi_io_num = mosi_pin,
        .miso_io_num = -1,
        .sclk_io_num = sclk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CONTROLLER_COLUMNS,
    };
    esp_err_t result = spi_bus_initialize(spi_host, &bus_config, SPI_DMA_CH_AUTO);
    if (result == ESP_OK) {
        owns_spi_bus = true;
    } else if (result != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(result);
    } else {
        ESP_LOGI(LOG_TAG, "SPI host already initialized; attaching display device");
    }

    const spi_device_interface_config_t device_config = {
        .mode = 3,
        .cs_ena_pretrans = 1,
        .cs_ena_posttrans = 1,
        .clock_speed_hz = static_cast<int>(frequency),
        .spics_io_num = cs_pin,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(spi_host, &device_config, &spi_device));

    xSemaphoreTake(mutex, portMAX_DELAY);

    // Manufacturer-recommended AQM1248A power-up sequence.
    const uint8_t initial[] = {
        0xAE, // display off
        0xA0, // ADC normal: column 0 maps to SEG0
        0xC8, // reverse COM output direction for this glass
        0xA3, // 1/7 LCD bias
        0x2C, // voltage converter on
    };
    write_commands(initial, sizeof(initial));
    vTaskDelay(pdMS_TO_TICKS(2));

    const uint8_t regulator = 0x2E;
    write_commands(&regulator, 1);
    vTaskDelay(pdMS_TO_TICKS(2));

    const uint8_t final[] = {
        0x2F,             // voltage follower on
        0x23,             // internal resistor ratio
        0x81, DEFAULT_CONTRAST,
        0xA4,             // normal display (not all points on)
        0x40,             // display start line 0
        0xA6,             // normal pixel polarity
    };
    write_commands(final, sizeof(final));
    clear_controller_ram();

    const uint8_t display_on = 0xAF;
    write_commands(&display_on, 1);
    DisplayDriver::set_brightness(DISP_BRIGHTNESS_100);

    xSemaphoreGive(mutex);
    ESP_LOGI(LOG_TAG, "Initialized at %u Hz", frequency);
}

void AQM1248ADriver::set_option(DriverOption opt, bool on) {
    if (opt != DISP_OPTION_POWER) return;

    // Display OFF + all-points ON enters the ST7565R's low-power sleep mode.
    // Restore normal all-points behavior before turning the display back on.
    const uint8_t power_on[] = {0xA4, 0xAF};
    const uint8_t power_off[] = {0xAE, 0xA5};
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (on) write_commands(power_on, sizeof(power_on));
    else write_commands(power_off, sizeof(power_off));
    xSemaphoreGive(mutex);
}

void AQM1248ADriver::set_brightness(Brightness brightness) {
    if (brightness >= DISP_BRIGHTNESS_MAX_INVALID) brightness = DISP_BRIGHTNESS_100;

    // The ST7565R electronic-volume setting controls LCD bias/contrast, not a
    // linear backlight. Values much below the module's recommended 0x1C make
    // this glass appear blank, so keep every UI level in its useful range.
    static constexpr uint8_t contrast_levels[DISP_BRIGHTNESS_MAX_INVALID] = {
        0x1C, // 0%: minimum visible contrast (power-off is a separate option)
        0x1E, // 25%
        0x20, // 50%
        0x22, // 75%
        0x24, // 100%
    };
    const uint8_t contrast = contrast_levels[static_cast<uint8_t>(brightness)];
    const uint8_t commands[] = {0x81, contrast};

    xSemaphoreTake(mutex, portMAX_DELAY);
    write_commands(commands, sizeof(commands));
    xSemaphoreGive(mutex);
    DisplayDriver::set_brightness(brightness);
}

void AQM1248ADriver::transfer(
    const std::vector<EGRect> changes,
    const BackingBuffer * buffer
) {
    if (changes.empty() || buffer == nullptr || buffer->data == nullptr ||
        buffer->fmt != EG_FMT_NATIVE ||
        buffer->size.width != DISPLAY_SIZE.width ||
        buffer->size.height != DISPLAY_SIZE.height) {
        return;
    }

    // One controller byte is a vertical group of eight pixels. Mark every
    // byte touched by an esper-gui dirty rectangle, including partially
    // covered pages; the backing buffer contains the other pixels in the byte.
    bool dirty[VISIBLE_PAGES][DISPLAY_SIZE.width] = {};
    for (const EGRect& rect : changes) {
        if (rect.size.width <= 0 || rect.size.height <= 0) continue;

        const int x0 = std::max(0, rect.origin.x);
        const int x1 = std::min(DISPLAY_SIZE.width, rect.origin.x + rect.size.width);
        const int y0 = std::max(0, rect.origin.y);
        const int y1 = std::min(DISPLAY_SIZE.height, rect.origin.y + rect.size.height);
        if (x0 >= x1 || y0 >= y1) continue;

        const int first_page = y0 / PAGE_HEIGHT;
        const int last_page = (y1 - 1) / PAGE_HEIGHT;
        for (int page = first_page; page <= last_page; ++page) {
            std::fill(dirty[page] + x0, dirty[page] + x1, true);
        }
    }

    const size_t stride = static_cast<size_t>(buffer->size.height / PAGE_HEIGHT);
    uint8_t pixels[DISPLAY_SIZE.width];

    xSemaphoreTake(mutex, portMAX_DELAY);
    for (int page = 0; page < VISIBLE_PAGES; ++page) {
        int x = 0;
        while (x < DISPLAY_SIZE.width) {
            while (x < DISPLAY_SIZE.width && !dirty[page][x]) ++x;
            const int first = x;
            while (x < DISPLAY_SIZE.width && dirty[page][x]) ++x;
            const int count = x - first;
            if (count == 0) continue;

            for (int column = 0; column < count; ++column) {
                // esper-gui stores the top pixel in the MSB; ST7565R page RAM
                // maps D0 to the top scan line.
                pixels[column] = reverse_bits(
                    buffer->data[(first + column) * stride + page]
                );
            }

            set_address(static_cast<uint8_t>(page), static_cast<uint8_t>(first));
            write_bytes(true, pixels, static_cast<size_t>(count));
        }
    }
    xSemaphoreGive(mutex);
}

uint8_t AQM1248ADriver::reverse_bits(uint8_t value) {
    value = static_cast<uint8_t>((value >> 4) | (value << 4));
    value = static_cast<uint8_t>(((value & 0xCC) >> 2) | ((value & 0x33) << 2));
    return static_cast<uint8_t>(((value & 0xAA) >> 1) | ((value & 0x55) << 1));
}

void AQM1248ADriver::write_bytes(bool data, const uint8_t * bytes, size_t count) {
    if (count == 0) return;

    ESP_ERROR_CHECK(gpio_set_level(rs_pin, data ? 1 : 0));
    spi_transaction_t transaction = {};
    transaction.length = count * 8;
    transaction.tx_buffer = bytes;
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi_device, &transaction));
}

void AQM1248ADriver::write_commands(const uint8_t * commands, size_t count) {
    write_bytes(false, commands, count);
}

void AQM1248ADriver::set_address(uint8_t page, uint8_t column) {
    const uint8_t commands[] = {
        static_cast<uint8_t>(0xB0 | (page & 0x0F)),
        static_cast<uint8_t>(0x10 | ((column >> 4) & 0x0F)),
        static_cast<uint8_t>(column & 0x0F),
    };
    write_commands(commands, sizeof(commands));
}

void AQM1248ADriver::clear_controller_ram() {
    uint8_t blank[CONTROLLER_COLUMNS];
    memset(blank, 0, sizeof(blank));
    for (int page = 0; page < CONTROLLER_PAGES; ++page) {
        set_address(static_cast<uint8_t>(page), 0);
        write_bytes(true, blank, sizeof(blank));
    }
}

}
#endif