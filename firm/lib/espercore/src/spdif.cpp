#include <esper-core/spdif.h>

static const char LOG_TAG[] = "SPDIF";

namespace Platform {
    void WM8805::initialize() {
        uint16_t devId = (read(Register::DEVID2) << 8) | read(Register::RST_DEVID1);
        uint8_t revision = read(Register::DEVREV);

        ESP_LOGI(LOG_TAG, "SPDIF init device %04x rev %x", devId, revision);

        if(devId != 0x8805) {
            ESP_LOGE(LOG_TAG, "Unexpected device id: %04x", devId);
        } else {
            write(Register::RST_DEVID1, 0); // reset the device

            write(Register::PLL5, PLL5Reg {{
                .frac_en = true,
                .mclkdiv = false,
                .clkout_div = PLL5Reg::ClkDiv::CLKDIV_512FS,
            }}.value);

            write(Register::PLL6, PLL6Reg {{
                .rx_in_select = 0, // RX0
                .clkout_src = 0,
                .clkout_dis = true,
                .fill_mode = true,
                .always_valid = false,
                .mclksrc = 0
            }}.value);

            write(Register::AIFRX, AIFRXReg {{
                .format = AIFRXReg::AIFRXFmt::I2S_FORMAT,
                .word_len = AIFRXReg::AIFRXWordLen::WLEN_16BIT,
                .bclk_invert = false,
                .lrck_polarity = false,
                .master = true,
                .sync = true
            }}.value);

            write(Register::PWRDN, PWRDNReg {{
                .pll = false,
                .spdif_rx = false,
                .spdif_tx = false,
                .osc = false,
                .aif = true, // keep i2s off until further notice
                .triop = false
            }}.value);

            // instead of unlock pin let's have a NON_AUDIO pin, cause otherwise we hear some noise on some drives when starting playback from stop position
            // see DS p.44 (GENERAL PURPOSE OUTPUT (GPO) CONFIGURATION)
            // write(Register::GPO01, 0x80); 

            if(pll_not_locked_gpio != GPIO_NUM_NC) {
                const gpio_config_t cfg = {
                    .pin_bit_mask = 1 << pll_not_locked_gpio,
                    .mode = GPIO_MODE_INPUT,
                    .pull_up_en = GPIO_PULLUP_DISABLE,
                    .pull_down_en = GPIO_PULLDOWN_DISABLE,
                };

                gpio_config(&cfg);
            }

            inited = true;
        }
    }

    void WM8805::set_enabled(bool enabled) {
        if(!inited) return;

        write(Register::PWRDN, PWRDNReg {{
            .pll = false,
            .spdif_rx = false,
            .spdif_tx = false,
            .osc = false,
            .aif = !enabled,
            .triop = false
        }}.value);
    }

    bool WM8805::need_deemphasis() {
        return get_fresh_status().deemphasis;
    }

    bool WM8805::locked_on() {
        if(pll_not_locked_gpio != GPIO_NUM_NC) {
            return !gpio_get_level(pll_not_locked_gpio);
        }
        return !get_fresh_status().unlock;
    }

    WM8805::SPDSTATReg WM8805::get_fresh_status() {
        TickType_t now = xTaskGetTickCount();
        // Avoid I2C congestion by limiting the reads to the status register
        if(now - last_sts_refresh >= pdMS_TO_TICKS(500)) {
            sts.value = read(Register::SPDSTAT);
        }
        return sts;
    }

    void WM8805::write(uint8_t regi, uint8_t val) {
        if(!i2c->lock()) {
            ESP_LOGE(LOG_TAG, "failed to acquire bus");
            return;
        }

        ESP_LOGD(LOG_TAG, "Write 0x%02x = 0x%02x", regi, val);

        auto wire = i2c->get();
        wire->beginTransmission(addr);
        wire->write(regi);
        wire->write(val);
        if(wire->endTransmission(true) != 0) ESP_LOGE(LOG_TAG, "I2C Write Failed");

        i2c->release();
    }

    uint8_t WM8805::read(uint8_t regi) {
        if(!i2c->lock()) {
            ESP_LOGE(LOG_TAG, "failed to acquire bus");
            return 255;
        }

        auto wire = i2c->get();
        wire->beginTransmission(addr);
        wire->write(regi);
        if(wire->endTransmission(false) != 0) {
            ESP_LOGE(LOG_TAG, "I2C Write Failed");
            i2c->release();
            return 255;
        }

        if(wire->requestFrom((int)addr, 1) != 1) {
            ESP_LOGE(LOG_TAG, "I2C request failed");
            i2c->release();
            return 255;
        }

        uint8_t data = wire->read();

        if(wire->endTransmission(true) != 0) {
            ESP_LOGE(LOG_TAG, "I2C endTransmission Failed");
            i2c->release();
            return 255;
        }

        i2c->release();

        ESP_LOGD(LOG_TAG, "Read 0x%02x = 0x%02x", regi, data);

        return data;
    }
}