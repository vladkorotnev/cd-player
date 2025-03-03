#pragma once

#include <mode.h>
#include "AudioTools.h"
#include "AudioTools/AudioLibs/A2DPStream.h"

using Platform::Button;

class BluetoothMode: public Mode {
public:
    BluetoothMode(const PlatformSharedResources, ModeHost *);
    ~BluetoothMode();
    void setup() override;
    UI::View& main_view() override;
    void loop() override;
    void on_key_pressed(VirtualKey key) override;
    void teardown() override;

private:
    class BluetoothView;
    BluetoothView * rootView;
    BluetoothA2DPSink a2dp;

    static void avrc_rn_playstatus_callback(esp_avrc_playback_stat_t);
    static void avrc_metadata_callback(uint8_t id, const uint8_t *text);

    void play_status_callback(esp_avrc_playback_stat_t);
    void metadata_callback(uint8_t id, const char *text);

    Button stopEject;
    Button playPause;
    Button next;
    Button prev;

    esp_avrc_playback_stat_t cur_sts;
};