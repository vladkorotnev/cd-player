#include <modes/netradio_mode.h>
#include <esper-gui/views/framework.h>
#include <views/wifi_icon.h>

using std::make_shared;
using std::shared_ptr;

static const char LOG_TAG[] = "APL_IRA";

const char *urls[] = {
    "http://icecast.ndr.de/ndr/ndr1wellenord/kiel/mp3/128/stream.mp3"
  };

void printMetaData(MetaDataType type, const char* str, int len){
    ESP_LOGI(LOG_TAG, "META['%s'] = '%s'", toStr(type), str);
}

class InternetRadioMode::InternetRadioView: public UI::View {
public:
    shared_ptr<WiFiIcon> wifi;
    shared_ptr<UI::TinySpinner> loading;


    InternetRadioView(EGRect f): View(f) {
        wifi = std::make_shared<WiFiIcon>(WiFiIcon({{0, 32 - 5}, {5, 5}}));
        loading = std::make_shared<UI::TinySpinner>(UI::TinySpinner({{0, 32 - 5}, {5, 5}}));
        loading->hidden = true;
        subviews.push_back(wifi);
        subviews.push_back(loading);
    }

};

InternetRadioMode::InternetRadioMode(const PlatformSharedResources res):
    urlStream(),
    source(urlStream, urls, "audio/mp3"),
    codec(),
    player(),
    sndTask("IRASND", 10000, configMAX_PRIORITIES - 2, 0),
    station_buttons({}), // todo
    chgSource(Button(res.keypad, (1 << 0))),
    moreStations(Button(res.keypad, (1 << 7))),
    Mode(res) {
    rootView = new InternetRadioView({{0, 0}, {160, 32}});
}

void InternetRadioMode::setup() {
    resources.router->activate_route(Platform::AudioRoute::ROUTE_INTERNAL_CPU);
    player.setAudioSource(source);
    player.setBufferSize(2048);
    urlStream.setReadBufferSize(2 * 1024 * 1024);
    player.setDecoder(codec);
    player.setOutput(*resources.router->get_output_port());
    player.setMetadataCallback(printMetaData);
    player.begin();
    sndTask.begin([this]() { player.copy(); });
}

void InternetRadioMode::loop() {
}

void InternetRadioMode::teardown() {
    sndTask.end();
    resources.router->activate_route(Platform::AudioRoute::ROUTE_NONE_INACTIVE);
}

InternetRadioMode::~InternetRadioMode() {
    delete rootView;
}

UI::View& InternetRadioMode::main_view() {
    return *rootView;
}