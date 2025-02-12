#include <esper-gui/futaba_gp1232.h>
#include <driver/uart.h>

static const char LOG_TAG[] = "GP1232";

namespace Graphics::Hardware {

    FutabaGP1232ADriver::FutabaGP1232ADriver(uart_port_t uart_num, gpio_num_t rx_pin, gpio_num_t tx_pin, int baud) {
        _port = uart_num;
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
    }

    void FutabaGP1232ADriver::initialize() {
        ESP_LOGI(LOG_TAG, "Reset");
        uart_write_bytes(_port, "\x1B\x0B", 2); // reset
        uart_write_bytes(_port, "\x1B\x0C", 2); // clear
        set_power(true);
        set_brightness(Brightness::DISP_BRIGHTNESS_100);
        uart_write_bytes(_port, "Hello! GP1232A", 15);
    }

    void FutabaGP1232ADriver::set_power(bool on) {
        ESP_LOGV(LOG_TAG, "Power = %i", on);
        const uint8_t cmd[] = {0x1B, 0x21, on ? 0x1 : 0x0};
        uart_write_bytes(_port, (const void*) cmd, 3);
    }

    void FutabaGP1232ADriver::set_brightness(Brightness brightness) {
        ESP_LOGV(LOG_TAG, "Brightness = %i", brightness);
        const uint8_t cmd[] = {0x1B, 0x20, brightness};
        uart_write_bytes(_port, (const void*) cmd, 3);
    }

    void FutabaGP1232ADriver::blit(const EGPoint address, const EGSize size, const BackingBuffer * buffer) {
        // NB: the pixels are arranged in a way so that the MSB is at the top in this display
        if(size.width == 0 || size.height == 0) return;

        const size_t stride = buffer->size.height / 8;
        const size_t start_idx_in_buffer = address.y / 8 + address.x * stride;
        const uint16_t w = (size.width); // mistake in datasheet? that says data size is (sX+1)*(sY + 1) so I expected this to be width-1, apparently not? using width-1 leads to one byte too much transfered. Why does it work in wacca-vfd-arduino?
        const uint8_t h = (size.height / 8 - 1);

        const uint8_t cmd[] = {0x1B, 0x2E, (address.x >> 8), (address.x & 0xFF), (address.y / 8), (w >> 8), (w & 0xFF), h};
        uart_write_bytes(_port, (const void*) cmd, 8);

        ESP_LOGV(LOG_TAG, "Blitting address=(%i, %i) size=(%i, %i) stride=%i sidx=%i", address.x, address.y, size.width, size.height, stride, start_idx_in_buffer);
        size_t written = 0;
        for(int col = 0; col < size.width; col++) {
            int out = uart_write_bytes(_port, buffer->data + (start_idx_in_buffer + col * stride), size.height / 8);
            if(out <= 0) ESP_LOGE(LOG_TAG, "Blit tx error!");
            else written += out;
        }
        ESP_LOGD(LOG_TAG, "Wrote %i data bytes", written);
    }
}