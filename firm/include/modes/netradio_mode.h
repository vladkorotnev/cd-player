#pragma once
#include <mode.h>

using Platform::Button;

class InternetRadioMode: public Mode {
public:
    InternetRadioMode(const PlatformSharedResources res, ModeHost *);
    ~InternetRadioMode();
    void setup() override;
    UI::View& main_view() override;
    void loop() override;
    void on_key_pressed(VirtualKey key) override;
    void teardown() override;

protected:
    void update_meta(MetaDataType type, const char * str, int len);
    static InternetRadioMode* _that;
    static void _update_meta_global(MetaDataType type, const char * str, int len);

    void play(const std::string url);
    void stop();
    void select_station(int index);
private:
    class InternetRadioView;
    InternetRadioView * rootView;
    class StreamingPipeline;
    StreamingPipeline * streamer;

    std::vector<Button> station_buttons;
    Button stopBtn;
    int cur_station_index = -1;
    TickType_t last_stream_health_check = 0;
};