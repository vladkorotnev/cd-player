#pragma once
#include "spdif.h"
#include <esp32-hal-gpio.h>
#include "AudioTools.h"
#define ESPER_AUDIO_BUFFER_SIZE 1024
#define ESPER_AUDIO_BUFFER_COUNT 4

namespace Platform {
    enum AudioRoute {
        /// @brief Audio output is disabled on hardware level, using a relay if present
        ROUTE_BYPASS,
        /// @brief Audio output is inactive or not initialized
        ROUTE_NONE_INACTIVE,
        /// @brief Audio output is routed from the CD Drive's SPDIF port to the DAC
        ROUTE_SPDIF_CD_PORT,
        /// @brief Audio output is routed from the ESP32's I2S bus
        ROUTE_INTERNAL_CPU,
    };

    struct DACBus {
        gpio_num_t unmute;
        gpio_num_t demph;
    };

    struct I2SBus {
        gpio_num_t mck;
        gpio_num_t bck;
        gpio_num_t lrck;
        gpio_num_t data;
    };

    class AudioRouter {
    public:
        AudioRouter(WM8805 * spdif, const DACBus dac, const I2SBus i2s, const gpio_num_t bypass_relay_gpio = GPIO_NUM_19);

        void activate_route(AudioRoute);
        void set_deemphasis(bool);

        AudioStream * get_io_port_nub();

    private:
        AudioRoute current_route = ROUTE_NONE_INACTIVE;
        WM8805 * _spdif;

        AudioInfo cpuOutputParams = AudioInfo(44100, 2, 16);
        AudioInfo cdaInputParams = AudioInfo(44100, 2, 16);
        I2SStream * _i2s;

        const DACBus dac_pins;
        const I2SBus i2s_pins;
        const gpio_num_t relay_pin;

        void i2s_bus_release_local();
        void i2s_bus_setup_local_output();
        void i2s_bus_setup_local_input();

        void spdif_set_up_muting_hax();
        void spdif_teardown_muting_hax();

        void set_mute_internal(bool);
        void set_bypass_relay(bool);
    };
};