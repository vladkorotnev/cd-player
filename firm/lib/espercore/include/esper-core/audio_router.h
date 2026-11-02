#pragma once
#include "spdif.h"
#include <esp32-hal-gpio.h>
#include "AudioTools.h"
#define ESPER_AUDIO_BUFFER_SIZE 512
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

    class AudioRouter {
        public:
            virtual void activate_route(AudioRoute) = 0;
            virtual AudioStream * get_io_port_nub() = 0;
            virtual void initialize() {}
    };

    class I2SOutputAudioRouter: public AudioRouter {
        public:
            struct I2SBus {
                gpio_num_t mck;
                gpio_num_t bck;
                gpio_num_t lrck;
                gpio_num_t data;
            };
            I2SOutputAudioRouter(const I2SBus i2s);
            AudioStream * get_io_port_nub() override;
            void activate_route(AudioRoute) override;
        protected:
            AudioRoute current_route = ROUTE_NONE_INACTIVE;
        private:
            bool bus_active = false;
            const I2SBus i2s_pins;
            I2SStream * _i2s;
            AudioInfo cpuOutputParams = AudioInfo(44100, 2, 16);
            void i2s_bus_release_local();
            void i2s_bus_setup_local_output();
    };

    class WM8805AudioRouter: public I2SOutputAudioRouter{
    public:
        struct DACBus {
            gpio_num_t unmute;
            gpio_num_t demph;
        };
        WM8805AudioRouter(WM8805 * spdif, const DACBus dac, const I2SBus i2s, const gpio_num_t bypass_relay_gpio);

        void initialize() override;
        void activate_route(AudioRoute) override;
        void set_deemphasis(bool);
    private:
        WM8805 * _spdif;

        AudioInfo cdaInputParams = AudioInfo(44100, 2, 16);

        const DACBus dac_pins;
        const gpio_num_t relay_pin;

        void spdif_set_up_muting_hax();
        void spdif_teardown_muting_hax();

        void set_mute_internal(bool);
        void set_bypass_relay(bool);
    };
};