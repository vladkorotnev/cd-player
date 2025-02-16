#include <esper-core/audio_router.h>
#include <hal/mcpwm_ll.h>
#include <driver/mcpwm.h>
#include <soc/dport_reg.h>
#include <soc/mcpwm_reg.h>

namespace Platform {
    AudioRouter::AudioRouter(WM8805 * spdif, const DACBus dac, const I2SBus i2s) :
        _spdif{spdif},
        dac_pins{dac},
        i2s_pins{i2s} {
            i2s_bus_release_local();

            gpio_config_t conf = { 0 };
            conf.mode = GPIO_MODE_OUTPUT;
            conf.pin_bit_mask = 
                (1 << dac_pins.demph) | (1 << dac_pins.unmute);
            gpio_config(&conf);

            set_deemphasis(false);
    }

    void AudioRouter::activate_route(AudioRoute next) {
        if(next == current_route) return;

        if(next == ROUTE_NONE_INACTIVE) {
            spdif_teardown_muting_hax();
            set_mute_internal(true); // shut it up
            _spdif->set_enabled(false);
            i2s_bus_release_local();
        }
        else if(next == ROUTE_SPDIF_CD_PORT) {
            i2s_bus_release_local();
            _spdif->set_enabled(true);
            spdif_set_up_muting_hax();
        }
        else if(next == ROUTE_INTERNAL_CPU) {
            spdif_teardown_muting_hax();
            _spdif->set_enabled(true);
            // TODO: set up i2s and unmute
        }

        current_route = next;
    }

    void AudioRouter::i2s_bus_release_local() {
        // Release I2S bus by setting pins into HiZ
        gpio_config_t conf = { 0 };
        conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        conf.pull_up_en = GPIO_PULLUP_DISABLE;
        conf.mode = GPIO_MODE_DISABLE;
        conf.pin_bit_mask = 
                (1 << i2s_pins.bck) | (1 << i2s_pins.lrck) | (1 << i2s_pins.data) | (1 << i2s_pins.mck);
        gpio_config(&conf);
    }

    void AudioRouter::set_deemphasis(bool demph) {
        gpio_set_level(dac_pins.demph, demph);
    }

    void AudioRouter::set_mute_internal(bool mute) {
        gpio_set_level(dac_pins.unmute, !mute);
    }

    void AudioRouter::spdif_set_up_muting_hax() {
        // TODO: this is too fast to react, use an interrupt instead?

        // Allocate a MCPWM unit at 500kHz (somehow more doesn't work for me).
        // Set duty cycle to 100% (a.k.a. constant logic HIGH)
        mcpwm_config_t mcpwm_config = {
            .frequency = 500000,
            .cmpr_a = 100.0,
            .cmpr_b = 100.0,
            .duty_mode = MCPWM_DUTY_MODE_0,
            .counter_mode = MCPWM_UP_COUNTER,
        };
        ESP_ERROR_CHECK(mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &mcpwm_config));
        ESP_ERROR_CHECK(mcpwm_stop(MCPWM_UNIT_0, MCPWM_TIMER_0));

        // Create emergency stop input on the MCPWM unit and tell it to force output (#MUTE) to logic LOW whenever (#LOCK) goes HIGH.
        ESP_ERROR_CHECK(mcpwm_fault_init(MCPWM_UNIT_0, MCPWM_HIGH_LEVEL_TGR, MCPWM_SELECT_F0));
        ESP_ERROR_CHECK(mcpwm_fault_set_cyc_mode(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_SELECT_F0, MCPWM_ACTION_FORCE_LOW, MCPWM_ACTION_FORCE_LOW));

        ESP_ERROR_CHECK(mcpwm_start(MCPWM_UNIT_0, MCPWM_TIMER_0));

        // The emergency stop interrupts will clog the heck out of our CPU, so let's get rid of that before too late.
        mcpwm_ll_intr_disable_all(&MCPWM0);

        // Technically use the MCPWM as a NOT gate, duh (notMute = notNotLocked, mute when not locked to an SPDIF stream)
        gpio_matrix_in(_spdif->pll_not_locked_gpio, PWM0_F0_IN_IDX, false);
        gpio_matrix_out(dac_pins.unmute, PWM0_OUT0A_IDX, false, false);
    }

    void AudioRouter::spdif_teardown_muting_hax() {
        // TODO revert all the hax above
    }
}