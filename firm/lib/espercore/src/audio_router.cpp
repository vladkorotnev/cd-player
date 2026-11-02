#include <esper-core/audio_router.h>
#include <hal/mcpwm_ll.h>
#include <driver/mcpwm.h>
#include <soc/dport_reg.h>
#include <soc/mcpwm_reg.h>

// Enable null output to allow JTAG to work
// #define ESPER_NULL_OUTPUT

#ifdef ESPER_NULL_OUTPUT
NullStream null;
AudioStreamWrapper nullAs(null);
#endif

static const char LOG_TAG[] = "AudioRouter";

namespace Platform {
    I2SOutputAudioRouter::I2SOutputAudioRouter(const I2SBus i2s):
        i2s_pins(i2s) {
        _i2s = new I2SStream();
    }

    WM8805AudioRouter::WM8805AudioRouter(WM8805 * spdif, const DACBus dac, const I2SBus i2s, const gpio_num_t bypass_relay_gpio) :
        _spdif{spdif},
        dac_pins{dac},
        relay_pin(bypass_relay_gpio),
        I2SOutputAudioRouter(i2s)
        {
        gpio_config_t conf = { 0 };
        conf.mode = GPIO_MODE_OUTPUT;
        conf.pin_bit_mask = 
            (1 << dac_pins.demph) | (1 << dac_pins.unmute);
        if(relay_pin != GPIO_NUM_NC) {
            conf.pin_bit_mask |= (1 << relay_pin);
        }
        gpio_config(&conf);

        set_deemphasis(false);
    }

    void WM8805AudioRouter::initialize() {
        _spdif->initialize();
        I2SOutputAudioRouter::initialize();
    }

    void I2SOutputAudioRouter::activate_route(AudioRoute next) {
        if (next == current_route) return;

        if (next == ROUTE_INTERNAL_CPU) {
            i2s_bus_setup_local_output();
        } else {
            i2s_bus_release_local();
        }

        current_route = next;
    }

    void WM8805AudioRouter::activate_route(AudioRoute next) {
        if(next == current_route) return;

        set_bypass_relay(next == ROUTE_BYPASS);

        if (current_route == ROUTE_INTERNAL_CPU) {
            // tear down the local bus before activating external one
            I2SOutputAudioRouter::activate_route(next);
        }

        if(next == ROUTE_NONE_INACTIVE || next == ROUTE_BYPASS) {
            spdif_teardown_muting_hax();
            set_mute_internal(true); // shut it up
            _spdif->set_enabled(false);
        }
        else if(next == ROUTE_SPDIF_CD_PORT) {
            _spdif->set_enabled(true);
            spdif_set_up_muting_hax();
        }
        else if(next == ROUTE_INTERNAL_CPU) {
            spdif_teardown_muting_hax();
            _spdif->set_enabled(false);
            set_mute_internal(false);
            set_deemphasis(false);
        }

        if (current_route != ROUTE_INTERNAL_CPU) {
            // set up the local bus after de-activating external one
            I2SOutputAudioRouter::activate_route(next);
        }
    }

    AudioStream * I2SOutputAudioRouter::get_io_port_nub() {
    #ifdef ESPER_NULL_OUTPUT
        return &nullAs;
    #else
        if(_i2s == nullptr) ESP_LOGE(LOG_TAG, "i2s nub IS NULL !!");
        return _i2s;
    #endif
    }

    void I2SOutputAudioRouter::i2s_bus_release_local() {
        if (!bus_active) return;

        ESP_LOGI(LOG_TAG, "Bus release");
        if(_i2s->isActive()) {
            _i2s->end();
        }
        // Release I2S bus by setting pins into HiZ
        gpio_config_t conf = { 0 };
        conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        conf.pull_up_en = GPIO_PULLUP_DISABLE;
        conf.mode = GPIO_MODE_DISABLE;
        conf.pin_bit_mask = 
                (1 << i2s_pins.bck) | (1 << i2s_pins.lrck) | (1 << i2s_pins.data) | (1 << i2s_pins.mck);
        gpio_config(&conf);
        bus_active = false;
    }

    void I2SOutputAudioRouter::i2s_bus_setup_local_output() {
        if (bus_active) return;
    #ifndef ESPER_NULL_OUTPUT
        ESP_LOGI(LOG_TAG, "Begin output");
        auto cfg = _i2s->defaultConfig(TX_MODE);
        cfg.copyFrom(cpuOutputParams);
        cfg.pin_bck = i2s_pins.bck;
        cfg.pin_data = i2s_pins.data;
        cfg.pin_mck = i2s_pins.mck;
        cfg.pin_ws = i2s_pins.lrck;
        cfg.buffer_count = ESPER_AUDIO_BUFFER_COUNT;
        cfg.buffer_size = ESPER_AUDIO_BUFFER_SIZE;
        _i2s->begin(cfg);
        ESP_LOGI(LOG_TAG, "Bus set to output");
    #endif
        bus_active = true;
    }

    void WM8805AudioRouter::set_deemphasis(bool demph) {
        gpio_set_level(dac_pins.demph, demph);
    }

    void WM8805AudioRouter::set_mute_internal(bool mute) {
        gpio_set_level(dac_pins.unmute, !mute);
    }

    void WM8805AudioRouter::spdif_set_up_muting_hax() {
        // Allocate a MCPWM unit at 1 Hz
        // Set duty cycle to 100% (a.k.a. constant logic HIGH)
        mcpwm_config_t mcpwm_config = {
            .frequency = 1,
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

    void WM8805AudioRouter::spdif_teardown_muting_hax() {
        ESP_ERROR_CHECK(mcpwm_stop(MCPWM_UNIT_0, MCPWM_TIMER_0));
        
        gpio_config_t conf = { 0 };
            conf.mode = GPIO_MODE_OUTPUT;
            conf.pin_bit_mask = 
                (1 << dac_pins.demph) | (1 << dac_pins.unmute);
            gpio_config(&conf);
    }

    void WM8805AudioRouter::set_bypass_relay(bool bypass) {
        if(relay_pin == GPIO_NUM_NC) return;
        gpio_set_level(relay_pin, !bypass);
    }
}