#pragma once

#include "atapi.h"
#include <array>

namespace ATAPI {
    /// Presents two independent devices on one IDE cable as a two-slot changer.
    /// Only the selected device is used for playback; inactive-slot media state
    /// is refreshed periodically while playback is stopped.
    class VirtualChangerDevice final : public Device {
    public:
        VirtualChangerDevice(Platform::IDEBus * bus, Device * master, Device * slave);

        void reset() override;
        void wait_ready() override;
        void eject(bool open_tray) override;
        void load_unload(SlotNumber slot) override;
        void start(bool state = true) override;
        void set_tray_locked(bool lock) override;
        void play(const MSF start, const MSF end) override;
        void pause(bool pause) override;
        void stop() override;
        void scan(bool forward, const MSF from) override;
        MediaTypeCode check_media() override;
        const DiscTOC read_toc() override;
        const DriveInfo * get_info() override;
        const MechInfo * query_state() override;
        const AudioStatus * query_position() override;
        const Quirks& get_quirks() override;
        const Diags * get_diags() override;

    private:
        static constexpr size_t DEVICE_COUNT = 2;
        static constexpr TickType_t INACTIVE_POLL_INTERVAL = pdMS_TO_TICKS(5000);

        Platform::IDEBus * ide;
        std::array<Device *, DEVICE_COUNT> devices;
        std::array<MediaTypeCode, DEVICE_COUNT> media = {
            MediaTypeCode::MTC_NO_DISC,
            MediaTypeCode::MTC_NO_DISC
        };
        std::array<bool, DEVICE_COUNT> media_known = {false, false};
        SlotNumber active = 0;
        TickType_t last_inactive_poll = 0;
        bool playback_active = false;
        bool switch_pending = false;
        MechInfo mech = {};

        Device& current() { return *devices[active]; }
        const Device& current() const { return *devices[active]; }
        void refresh_media(SlotNumber slot);
        static bool media_present(MediaTypeCode code);
    };
}
