#include <esper-gui/hardware/futaba_gp1232.h>
#include <driver/uart.h>
#include <algorithm>

static const char LOG_TAG[] = "GP1232";

static const char init_test_string[] = "ESPer-CDP";

namespace Graphics::Hardware {

    FutabaGP1232ADriver::FutabaGP1232ADriver(uart_port_t uart_num, gpio_num_t rx_pin, gpio_num_t tx_pin, int baud) {
        _port = uart_num;
        _portSema = xSemaphoreCreateBinary();
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
    }

    void FutabaGP1232ADriver::initialize() {
        ESP_LOGI(LOG_TAG, "Reset");
        xSemaphoreTake(_portSema, portMAX_DELAY);
        uart_write_bytes(_port, "\x1B\x0B", 2); // reset
        uart_write_bytes(_port, "\x1B\x0C", 2); // clear
        xSemaphoreGive(_portSema);
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

    void FutabaGP1232ADriver::transfer(const std::vector<EGRect> changes, const BackingBuffer * buffer) {
        for(auto& rect: changes) {
            transfer(rect.origin, rect.size, buffer);
        }

        if(anti_tearing) {
            flip_pages();
            for(auto& rect: changes) {
                transfer(rect.origin, rect.size, buffer);
            }
        }
    }

    void FutabaGP1232ADriver::flip_pages() {
        int new_dsa = (visible_dsa == DSA_FIRST ? DSA_SECOND : DSA_FIRST);
        const uint8_t cmd[] = {0x1B, 0x22, (uint8_t)(new_dsa >> 8), (uint8_t)(new_dsa & 0xFF)};
        xSemaphoreTake(_portSema, portMAX_DELAY);
        uart_write_bytes(_port, (const void*) cmd, 4);
        xSemaphoreGive(_portSema);
        visible_dsa = new_dsa;
    }

    void FutabaGP1232ADriver::transfer(const EGPoint address, const EGSize size, const BackingBuffer * buffer) {
        // NB: the pixels are arranged in a way so that the MSB is at the top in this display
        if(size.width == 0 || size.height == 0) return;

        const uint16_t w = (size.width);
        const uint8_t h = (size.height / 8) + ((size.height % 8) != 0);
        const size_t stride = buffer->size.height / 8;
        const size_t start_idx_in_buffer = address.y / 8 + address.x * stride;

        int actual_x = address.x;
        if(anti_tearing) actual_x += (visible_dsa == DSA_FIRST ? DSA_SECOND : DSA_FIRST);

        const uint8_t cmd[] = {0x1B, 0x2E, (uint8_t) (actual_x >> 8), (uint8_t) (actual_x & 0xFF), (uint8_t) (address.y / 8), (uint8_t) (w >> 8), (uint8_t) (w & 0xFF), (uint8_t) (h - 1)};
        
        xSemaphoreTake(_portSema, portMAX_DELAY);
        uart_write_bytes(_port, (const void*) cmd, 8);

        ESP_LOGV(LOG_TAG, "Blitting address=(%i, %i) size=(%i, %i) stride=%i sidx=%i", address.x, address.y, w, h, stride, start_idx_in_buffer);
        size_t written = 0;
        for(int col = 0; col < w; col++) {
            int out = uart_write_bytes(_port, buffer->data + (start_idx_in_buffer + col * stride), h);
            if(out <= 0) ESP_LOGE(LOG_TAG, "Blit tx error!");
            else written += out;
        }
        xSemaphoreGive(_portSema);
        ESP_LOGD(LOG_TAG, "Wrote %i data bytes", written);
    }
}