#pragma once
#include <mode.h>
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/AudioCodecs/CodecAACHelix.h"

using Platform::Button;

class InternetRadioMode: public Mode {
public:
    InternetRadioMode(const PlatformSharedResources res);
    ~InternetRadioMode();
    void setup() override;
    UI::View& main_view() override;
    void loop() override;
    void teardown() override;

protected:
    void update_meta(MetaDataType type, const char * str, int len);
    static InternetRadioMode* _that;
    static void _update_meta_global(MetaDataType type, const char * str, int len);
private:
    class InternetRadioView;
    InternetRadioView * rootView;

    std::vector<Button> station_buttons;
    Button chgSource;
    Button moreStations;

    AudioPlayer player;
    MP3DecoderHelix codec;
    AudioSourceURL source;
    ICYStreamBuffered urlStream;
    
    Task sndTask;
};