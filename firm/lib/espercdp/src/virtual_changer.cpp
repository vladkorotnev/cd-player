#include <esper-cdp/virtual_changer.h>
#include <esper-core/lock.h>

namespace {
    constexpr char LOG_TAG[] = "ATAPI_CHANGER";
}

namespace ATAPI {
    VirtualChangerDevice::VirtualChangerDevice(Platform::IDEBus * bus, Device * master, Device * slave):
        ide(bus), devices({master, slave})
    {
        mech.slot_count = DEVICE_COUNT;
        mech.current_disc = active;
        mech.changer_state = MechInfo::ChangerState::Idle;
    }

    bool VirtualChangerDevice::media_present(MediaTypeCode code) {
        return code != MediaTypeCode::MTC_NO_DISC &&
               code != MediaTypeCode::MTC_DOOR_OPEN;
    }

    void VirtualChangerDevice::refresh_media(SlotNumber slot) {
        const bool was_present = media_known[slot] && media_present(media[slot]);
        media[slot] = devices[slot]->check_media();
        const bool is_present = media_present(media[slot]);
        mech.changer_slots[slot].disc_changed = media_known[slot] && was_present != is_present;
        mech.changer_slots[slot].disc_in = is_present;
        media_known[slot] = true;
    }

    void VirtualChangerDevice::reset() {
        playback_active = false;
        switch_pending = false;
        {
            Lock lock(ide->transaction_mutex());
            ide->reset();
        }
        for (auto * device : devices) device->reset();
        media_known = {false, false};
        refresh_media(0);
        refresh_media(1);
    }

    void VirtualChangerDevice::wait_ready() {
        current().wait_ready();
        switch_pending = false;
        mech.is_in_motion = false;
        mech.changer_state = MechInfo::ChangerState::Idle;
        refresh_media(active);
    }

    void VirtualChangerDevice::eject(bool open_tray) {
        playback_active = false;
        current().eject(open_tray);
        media_known[active] = false;
    }

    void VirtualChangerDevice::load_unload(SlotNumber slot) {
        if (slot >= devices.size() || slot == active) return;
        if (playback_active) current().stop();
        playback_active = false;
        active = slot;
        mech.current_disc = active;
        mech.is_in_motion = true;
        mech.changer_state = MechInfo::ChangerState::Changing_Disc;
        switch_pending = true;
        ESP_LOGI(LOG_TAG, "Selected virtual slot %u", active);
        current().start(true);
    }

    void VirtualChangerDevice::start(bool state) {
        if (state) {
            current().start(true);
            return;
        }

        playback_active = false;
        for (auto * device : devices) device->start(false);
    }

    void VirtualChangerDevice::set_tray_locked(bool locked) {
        for (auto * device : devices) device->set_tray_locked(locked);
    }

    void VirtualChangerDevice::play(const MSF start, const MSF end) {
        current().play(start, end);
        playback_active = true;
    }

    void VirtualChangerDevice::pause(bool paused) {
        current().pause(paused);
    }

    void VirtualChangerDevice::stop() {
        current().stop();
        playback_active = false;
    }

    void VirtualChangerDevice::scan(bool forward, const MSF from) {
        current().scan(forward, from);
        playback_active = true;
    }

    MediaTypeCode VirtualChangerDevice::check_media() {
        if (switch_pending) return media[active];
        refresh_media(active);
        return media[active];
    }

    const DiscTOC VirtualChangerDevice::read_toc() {
        return current().read_toc();
    }

    const DriveInfo * VirtualChangerDevice::get_info() {
        return current().get_info();
    }

    const MechInfo * VirtualChangerDevice::query_state() {
        if (switch_pending) return &mech;

        const MechInfo * physical = current().query_state();
        if (!media_known[active]) refresh_media(active);
        const TickType_t now = xTaskGetTickCount();
        if (!playback_active &&
            (!media_known[1 - active] || now - last_inactive_poll >= INACTIVE_POLL_INTERVAL)) {
            refresh_media(1 - active);
            last_inactive_poll = now;
        }

        mech.slot_count = DEVICE_COUNT;
        mech.is_door_open = physical->is_door_open;
        mech.is_playing = playback_active || physical->is_playing;
        mech.is_fault = physical->is_fault;
        mech.is_in_motion = physical->is_in_motion;
        mech.current_disc = active;
        mech.changer_state = MechInfo::ChangerState::Idle;
        return &mech;
    }

    const AudioStatus * VirtualChangerDevice::query_position() {
        const AudioStatus * position = current().query_position();
        if (position->state == AudioStatus::PlayState::Stopped) playback_active = false;
        return position;
    }

    const Device::Quirks& VirtualChangerDevice::get_quirks() {
        return current().get_quirks();
    }

    const Device::Diags * VirtualChangerDevice::get_diags() {
        return current().get_diags();
    }
}
