#include <esper-gui/hardware/futaba_gp1232.h>
#include <driver/uart.h>

static const char LOG_TAG[] = "GP1232";

static const char init_test_string[] = "ESPer-CDP";

namespace Graphics::Hardware {

    FutabaGP1232ADriver::FutabaGP1232ADriver(uart_port_t uart_num, gpio_num_t rx_pin, gpio_num_t tx_pin, int baud) {
        _port = uart_num;
        _portSema = xSemaphoreCreateBinary();
        _frameSema = xSemaphoreCreateBinary();
        const uart_config_t uart_config = {
            .baud_rate = baud,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        };

        ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
        ESP_ERROR_CHECK(uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        // Setup UART buffered IO with event queue
        const int uart_buffer_size = (1024 * 2);
        QueueHandle_t uart_queue;
        ESP_ERROR_CHECK(uart_driver_install(uart_num, SOC_UART_FIFO_LEN+1 /* no use of Rx in this driver, but buffer must be >SOC_UART_FIFO_LEN */, uart_buffer_size, 10, &uart_queue, 0));

        ESP_LOGI(LOG_TAG, "UART init.");
        xSemaphoreGive(_portSema);
        xSemaphoreGive(_frameSema);
    }

    void FutabaGP1232ADriver::initialize() {
        ESP_LOGI(LOG_TAG, "Reset");
        page_valid[0] = page_valid[1] = false;
        visible_dsa = DSA_FIRST;
        xSemaphoreTake(_portSema, portMAX_DELAY);
        uart_write_bytes(_port, "\x1B\x0B", 2); // reset
        uart_write_bytes(_port, "\x1B\x0C", 2); // clear
        xSemaphoreGive(_portSema);
        set_visible_dsa(DSA_FIRST);
        set_power(true);
        set_brightness(Brightness::DISP_BRIGHTNESS_100);
        xSemaphoreTake(_portSema, portMAX_DELAY);
        uart_write_bytes(_port, init_test_string, strlen(init_test_string));
        xSemaphoreGive(_portSema);
    }

    void FutabaGP1232ADriver::set_power(bool on) {
        ESP_LOGV(LOG_TAG, "Power = %i", on);
        const uint8_t cmd[] = {0x1B, 0x21, (uint8_t)(on ? 0x1 : 0x0)};
        xSemaphoreTake(_portSema, portMAX_DELAY);
        uart_write_bytes(_port, (const void*) cmd, 3);
        xSemaphoreGive(_portSema);
    }

    void FutabaGP1232ADriver::set_brightness(Brightness brightness) {
        ESP_LOGV(LOG_TAG, "Brightness = %i", brightness);
        const uint8_t cmd[] = {0x1B, 0x20, brightness};
        xSemaphoreTake(_portSema, portMAX_DELAY);
        uart_write_bytes(_port, (const void*) cmd, 3);
        xSemaphoreGive(_portSema);
        DisplayDriver::set_brightness(brightness);
    }

    void FutabaGP1232ADriver::set_anti_tearing(bool on) {
        xSemaphoreTake(_frameSema, portMAX_DELAY);
        anti_tearing = on;
        xSemaphoreGive(_frameSema);
    }

    void FutabaGP1232ADriver::set_option(DriverOption opt, bool on) {
        switch (opt) {
            case DriverOption::DISP_OPTION_POWER:
                set_power(on);
                break;

            case DriverOption::DISP_OPTION_VSYNC:
                set_anti_tearing(on);
                break;
        }
    }

    void FutabaGP1232ADriver::transfer(const std::vector<EGRect> changes, const BackingBuffer * buffer) {
        xSemaphoreTake(_frameSema, portMAX_DELAY);
        const int visible_page = (visible_dsa == DSA_FIRST ? 0 : 1);
        if (!changes.empty() && page_differs(visible_page, buffer)) {
            const int target_page = anti_tearing ? 1 - visible_page : visible_page;
            if (page_differs(target_page, buffer)) {
                const int target_dsa = (target_page == 0 ? DSA_FIRST : DSA_SECOND);
                if (!page_valid[target_page]) {
                    transfer({0, 0}, DISPLAY_SIZE, buffer, target_dsa);
                } else {
                    // A gap of up to eight unchanged bytes costs no more than another blit command.
                    auto scan_runs = [&](bool send) {
                        size_t cost = 0;
                        const int stride = buffer->size.height / PAGE_HEIGHT;
                        for (int row = 0; row < DISPLAY_HEIGHT / PAGE_HEIGHT; ++row) {
                            int x = 0;
                            while (x < DISPLAY_WIDTH) {
                                while (x < DISPLAY_WIDTH &&
                                       page_shadow[target_page][x * stride + row] == buffer->data[x * stride + row]) {
                                    ++x;
                                }
                                if (x == DISPLAY_WIDTH) break;

                                const int first = x;
                                int last_changed = x;
                                for (++x; x < DISPLAY_WIDTH; ++x) {
                                    if (page_shadow[target_page][x * stride + row] != buffer->data[x * stride + row]) {
                                        last_changed = x;
                                    }
                                    if (x - last_changed > 8) break;
                                }

                                const int width = last_changed - first + 1;
                                cost += 8 + width;
                                if (send) transfer({first, row * PAGE_HEIGHT}, {width, PAGE_HEIGHT}, buffer, target_dsa);
                                x = last_changed + 1;
                            }
                        }
                        return cost;
                    };

                    if (scan_runs(false) >= FRAME_BYTES + 8) {
                        transfer({0, 0}, DISPLAY_SIZE, buffer, target_dsa);
                    } else {
                        scan_runs(true);
                    }
                }
                update_page(target_page, buffer);
            }
            if (anti_tearing) flip_pages();
        }
        xSemaphoreGive(_frameSema);
    }

    bool FutabaGP1232ADriver::page_differs(int page, const BackingBuffer * buffer) const {
        return !page_valid[page] || memcmp(page_shadow[page], buffer->data, FRAME_BYTES) != 0;
    }

    void FutabaGP1232ADriver::update_page(int page, const BackingBuffer * buffer) {
        memcpy(page_shadow[page], buffer->data, FRAME_BYTES);
        page_valid[page] = true;
    }

    void FutabaGP1232ADriver::set_visible_dsa(int dsa) {
        const uint8_t cmd[] = {0x1B, 0x22, (uint8_t)(dsa >> 8), (uint8_t)(dsa & 0xFF)};
        xSemaphoreTake(_portSema, portMAX_DELAY);
        uart_write_bytes(_port, (const void*) cmd, 4);
        xSemaphoreGive(_portSema);
        visible_dsa = dsa;
    }

    void FutabaGP1232ADriver::flip_pages() {
        int new_dsa = (visible_dsa == DSA_FIRST ? DSA_SECOND : DSA_FIRST);
        set_visible_dsa(new_dsa);
    }

    void FutabaGP1232ADriver::transfer(const EGPoint address, const EGSize size, const BackingBuffer * buffer, int dsa) {
        // NB: the pixels are arranged in a way so that the MSB is at the top in this display
        if(size.width == 0 || size.height == 0) return;

        const uint16_t w = (size.width);
        const uint8_t h = ((address.y % 8) + size.height + 7) / 8;
        const size_t stride = buffer->size.height / 8;
        const size_t start_idx_in_buffer = address.y / 8 + address.x * stride;

        const int actual_x = address.x + dsa;

        const uint8_t cmd[] = {0x1B, 0x2E, (uint8_t) (actual_x >> 8), (uint8_t) (actual_x & 0xFF), (uint8_t) (address.y / 8), (uint8_t) (w >> 8), (uint8_t) (w & 0xFF), (uint8_t) (h - 1)};
        
        xSemaphoreTake(_portSema, portMAX_DELAY);
        uart_write_bytes(_port, (const void*) cmd, 8);

        ESP_LOGV(LOG_TAG, "Blitting address=(%i, %i) size=(%i, %i) stride=%i sidx=%i", address.x, address.y, w, h, stride, start_idx_in_buffer);
        uint8_t pixels[FRAME_BYTES];
        for (int col = 0; col < w; ++col) {
            memcpy(pixels + col * h, buffer->data + start_idx_in_buffer + col * stride, h);
        }
        int out = uart_write_bytes(_port, pixels, w * h);
        if(out <= 0) ESP_LOGE(LOG_TAG, "Blit tx error!");
        xSemaphoreGive(_portSema);
        ESP_LOGD(LOG_TAG, "Wrote %i data bytes", out > 0 ? out : 0);
    }
}
