#pragma once
#include <esper-core/ide.h>
#include "types.h"
#include "atapi.h"

namespace ATAPI {
    class AutonomousPlayerDevice: public HardwareATAPIDevice {
    public:
        AutonomousPlayerDevice(Platform::IDEBus * bus): HardwareATAPIDevice(bus) {}

        void play(const MSF start, const MSF end) override;
        void pause(bool pause) override;
        void stop() override;
        void scan(bool forward, const MSF from) override;
        const AudioStatus * query_position() override;
        bool initialize() override;

    private:
        AudioStatus audio_sts = { 0 };
        bool playback_mode_select_flag = false;

        void mode_select_output_ports();
        void mode_select_power_conditions();
    };
}
