#pragma once
#include <mode.h>
#include "AudioTools.h"
#include <AudioTools/AudioCodecs/CodecMP3Helix.h>
#include <AudioTools/AudioCodecs/CodecAACHelix.h>

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

    void play();
private:
    class InternetRadioView;
    InternetRadioView * rootView;

    std::vector<Button> station_buttons;
    Button chgSource;
    Button moreStations;

    StreamCopy copier;
    MP3DecoderHelix mp3;
    EncodedAudioStream decoder;
    ICYStreamBuffered urlStream;
    
    Task * sndTask;
};