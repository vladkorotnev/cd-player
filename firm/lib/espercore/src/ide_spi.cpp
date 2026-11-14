#include <esper-core/ide.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>

namespace {
    constexpr char LOG_TAG[] = "IDE_SPI";
    constexpr int SPI_CLOCK_HZ = 5000000;
    // Four SPI bytes per PIO word, including the discarded recovery samples.
    constexpr size_t DMA_BUFFER_SIZE = 8192;
    constexpr unsigned ONE_SHOT_SETTLE_US = 5;

    // JP1-JP3 strap U3 to address 0, and JP4-JP6 strap U9 to address 1.
    constexpr uint8_t DATA_EXPANDER = 0x40;
    constexpr uint8_t CONTROL_EXPANDER = 0x42;
    constexpr uint8_t SPI_READ = 0x01;
    constexpr uint8_t IODIR_A = 0x00;
    constexpr uint8_t IPOL_A = 0x02;
    constexpr uint8_t IOCON = 0x0A;
    constexpr uint8_t GPPU_A = 0x0C;
    constexpr uint8_t GPIO_A = 0x12;
    constexpr uint8_t OLAT_A = 0x14;
    constexpr uint8_t IOCON_CONFIG = 0x28; // BANK=0, SEQOP=1, HAEN=1

    constexpr uint8_t ADDRESS_AND_CS_MASK = 0x1F;
    constexpr uint8_t DRIVE_DESELECTED = 0x18; // Both active-low IDE selects high.
    constexpr uint8_t DRIVE_RESET_N = 0x20;   // U9 GPA5.
    constexpr uint8_t CONTROL_IDLE = DRIVE_DESELECTED | DRIVE_RESET_N;

    void output_high(gpio_num_t pin) {
        gpio_set_level(pin, 1);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    }
}

namespace Platform {
    SPIBridgedIDEBus::SPIBridgedIDEBus(spi_host_device_t host, Pins pins):
        host(host), pins(pins) {}

    SPIBridgedIDEBus::~SPIBridgedIDEBus() {
        if (device != nullptr) {
            spi_bus_remove_device(device);
            spi_bus_free(host);
        }
        heap_caps_free(dma_buffer);
    }

    bool SPIBridgedIDEBus::begin() {
        if (device != nullptr) return ready;

        output_high(pins.cs);
        output_high(pins.burst);
        output_high(pins.write);
        if (pins.reset != GPIO_NUM_NC) {
            output_high(pins.reset);
            gpio_set_level(pins.reset, 0);
            delayMicroseconds(10);
            gpio_set_level(pins.reset, 1);
        }
        delay(1);

        dma_buffer = static_cast<uint8_t *>(heap_caps_malloc(DMA_BUFFER_SIZE, MALLOC_CAP_DMA));
        if (dma_buffer == nullptr) {
            ESP_LOGE(LOG_TAG, "Could not allocate SPI DMA buffer");
            return false;
        }
        const spi_bus_config_t bus_config = {
            .mosi_io_num = pins.mosi,
            .miso_io_num = pins.miso,
            .sclk_io_num = pins.sck,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = DMA_BUFFER_SIZE,
        };
        const spi_device_interface_config_t device_config = {
            .command_bits = 8,
            .address_bits = 8,
            .mode = 0,
            .cs_ena_pretrans = 1,
            .cs_ena_posttrans = 1,
            .clock_speed_hz = SPI_CLOCK_HZ,
            .spics_io_num = pins.cs,
            .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY,
            .queue_size = 1,
        };
        esp_err_t result = spi_bus_initialize(host, &bus_config, SPI_DMA_CH_AUTO);
        if (result == ESP_OK) {
            result = spi_bus_add_device(host, &device_config, &device);
            if (result != ESP_OK) spi_bus_free(host);
        }
        if (result != ESP_OK) {
            ESP_LOGE(LOG_TAG, "SPI setup failed: %s", esp_err_to_name(result));
            heap_caps_free(dma_buffer);
            dma_buffer = nullptr;
            return false;
        }

        // At power-on both chips accept 0x40 until the shared write enables HAEN.
        // The addressed write also works when the ESP reboots with HAEN already set.
        ready = write_expander(DATA_EXPANDER, IOCON, IOCON_CONFIG) &&
                write_expander(CONTROL_EXPANDER, IOCON, IOCON_CONFIG) &&
                check_expander(DATA_EXPANDER) && check_expander(CONTROL_EXPANDER) &&
                write_expander(DATA_EXPANDER, IODIR_A, 0xFF, 0xFF, 2) &&
                write_expander(DATA_EXPANDER, IPOL_A, 0, 0, 2) &&
                write_expander(DATA_EXPANDER, GPPU_A, 0xFF, 0xFF, 2) &&
                write_expander(CONTROL_EXPANDER, IPOL_A, 0) &&
                write_expander(CONTROL_EXPANDER, OLAT_A, CONTROL_IDLE) &&
                write_expander(CONTROL_EXPANDER, IODIR_A, 0xC0);
        if (ready) ESP_LOGI(LOG_TAG, "SPI IDE bridge ready at %d Hz", SPI_CLOCK_HZ);
        return ready;
    }

    bool SPIBridgedIDEBus::transfer(spi_transaction_t& transaction) {
        const esp_err_t result = spi_device_polling_transmit(device, &transaction);
        if (result == ESP_OK) return true;

        ESP_LOGE(LOG_TAG, "SPI transfer failed: %s", esp_err_to_name(result));
        // A failed payload may leave hardware CS asserted after its header.
        gpio_set_level(pins.cs, 1);
        esp_rom_gpio_connect_out_signal(pins.cs, SIG_GPIO_OUT_IDX, false, false);
        gpio_set_level(pins.burst, 1);
        gpio_set_level(pins.write, 1);
        ready = false;
        return false;
    }

    bool SPIBridgedIDEBus::write_expander(uint8_t opcode, uint8_t reg, uint8_t low, uint8_t high, size_t bytes) {
        spi_transaction_t transaction = {};
        transaction.flags = SPI_TRANS_USE_TXDATA;
        transaction.cmd = opcode;
        transaction.addr = reg;
        transaction.length = bytes * 8;
        transaction.tx_data[0] = low;
        transaction.tx_data[1] = high;
        return transfer(transaction);
    }

    bool SPIBridgedIDEBus::check_expander(uint8_t opcode) {
        spi_transaction_t transaction = {};
        transaction.flags = SPI_TRANS_USE_RXDATA;
        transaction.cmd = opcode | SPI_READ;
        transaction.addr = IOCON;
        transaction.rxlength = 8;
        if (!transfer(transaction)) return false;
        if (transaction.rx_data[0] == IOCON_CONFIG) return true;

        ESP_LOGE(LOG_TAG, "MCP23S17 opcode 0x%02x: IOCON read 0x%02x, expected 0x%02x",
                 opcode, transaction.rx_data[0], IOCON_CONFIG);
        return false;
    }

    void SPIBridgedIDEBus::reset() {
        if (!ready) return;
        selected_device = 0xFF;
        ESP_LOGI(LOG_TAG, "Resetting drive through U9");
        if (!write_expander(CONTROL_EXPANDER, OLAT_A, DRIVE_DESELECTED)) return;
        delay(40);
        if (!write_expander(CONTROL_EXPANDER, OLAT_A, CONTROL_IDLE)) return;
        delay(20);
        selected_device = IDE::IDE_MASTER; // ATA reset selects device 0.
    }

    bool SPIBridgedIDEBus::select_device() {
        if (!ready) return false;
        if (selected_device == active_device) return true;

        const uint8_t select = active_device == IDE::IDE_SLAVE ? 0xB0 : 0xA0;
        if (!write_word(IDE::DriveSelect, {{select, 0xFF}})) return false;
        delayMicroseconds(1);
        selected_device = active_device;
        return true;
    }

    data16 SPIBridgedIDEBus::read(uint8_t reg) {
        if (!select_device() ||
            !write_expander(CONTROL_EXPANDER, OLAT_A, DRIVE_RESET_N | (reg & ADDRESS_AND_CS_MASK))) {
            return {{0xFF, 0xFF}};
        }

        // With BURST high, CS asserts DIOR for the entire SPI frame.
        gpio_set_level(pins.write, 0);
        spi_transaction_t transaction = {};
        transaction.flags = SPI_TRANS_USE_RXDATA;
        transaction.cmd = DATA_EXPANDER | SPI_READ;
        transaction.addr = GPIO_A;
        transaction.rxlength = 16;
        const bool received = transfer(transaction);
        gpio_set_level(pins.write, 1);
        if (!received || !write_expander(CONTROL_EXPANDER, OLAT_A, CONTROL_IDLE)) {
            return {{0xFF, 0xFF}};
        }
        return {{transaction.rx_data[0], transaction.rx_data[1]}};
    }

    bool SPIBridgedIDEBus::clock_payload(uint8_t opcode, spi_transaction_ext_t& payload) {
        const esp_err_t result = spi_device_acquire_bus(device, portMAX_DELAY);
        if (result != ESP_OK) {
            ESP_LOGE(LOG_TAG, "Could not acquire SPI bus: %s", esp_err_to_name(result));
            return false;
        }

        spi_transaction_t header = {};
        header.flags = SPI_TRANS_CS_KEEP_ACTIVE;
        header.cmd = opcode;
        header.addr = GPIO_A;
        bool success = transfer(header);
        if (success) {
            // SCK is stopped low here. Count only the payload clocks.
            gpio_set_level(pins.burst, 0);
            delayMicroseconds(1);
            payload.base.flags |= SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
            success = transfer(payload.base);
            // Hardware has released CS. Hold data/address through the last one-shot.
            delayMicroseconds(ONE_SHOT_SETTLE_US);
            gpio_set_level(pins.burst, 1);
        }
        spi_device_release_bus(device);
        return success;
    }

    bool SPIBridgedIDEBus::read_data(uint8_t * buffer, size_t words) {
        if (words == 0) return true;
        if (buffer == nullptr || words > DMA_BUFFER_SIZE / 4) {
            ESP_LOGE(LOG_TAG, "Invalid DMA read size or buffer (%u words)", words);
            return false;
        }
        if (!select_device() ||
            !write_expander(CONTROL_EXPANDER, OLAT_A, DRIVE_RESET_N | (IDE::Data & ADDRESS_AND_CS_MASK))) {
            return false;
        }

        gpio_set_level(pins.write, 0);
        spi_transaction_ext_t payload = {};
        // Between adjacent drive words, one 16-bit SPI sample spans DIOR recovery.
        // The allocation leaves room for DMA's final 32-bit store. Clock only
        // the requested bytes so the bridge does not start another IDE read.
        payload.base.rxlength = (4 * words - 2) * 8;
        payload.base.rx_buffer = dma_buffer;
        const bool received = clock_payload(DATA_EXPANDER | SPI_READ, payload);
        gpio_set_level(pins.write, 1);
        if (!ready || !write_expander(CONTROL_EXPANDER, OLAT_A, CONTROL_IDLE) || !received) {
            return false;
        }
        for (size_t word = 0; word < words; ++word) {
            buffer[2 * word] = dma_buffer[4 * word];
            buffer[2 * word + 1] = dma_buffer[4 * word + 1];
        }
        return true;
    }

    void SPIBridgedIDEBus::write(uint8_t reg, data16 data) {
        if (select_device()) write_word(reg, data);
    }

    bool SPIBridgedIDEBus::write_word(uint8_t reg, data16 data) {
        if (!write_expander(CONTROL_EXPANDER, OLAT_A, DRIVE_RESET_N | (reg & ADDRESS_AND_CS_MASK)) ||
            !write_expander(DATA_EXPANDER, IODIR_A, 0, 0, 2)) {
            return false;
        }

        // WRITE is high, and BURST has masked any pulse from releasing U6B reset.
        delayMicroseconds(ONE_SHOT_SETTLE_US);
        spi_transaction_ext_t payload = {};
        payload.base.flags = SPI_TRANS_USE_TXDATA;
        payload.base.length = 16;
        payload.base.tx_data[0] = data.low;
        payload.base.tx_data[1] = data.high;
        const bool written = clock_payload(DATA_EXPANDER, payload);
        return ready &&
               write_expander(DATA_EXPANDER, IODIR_A, 0xFF, 0xFF, 2) &&
               write_expander(CONTROL_EXPANDER, OLAT_A, CONTROL_IDLE) &&
               written;
    }
}
