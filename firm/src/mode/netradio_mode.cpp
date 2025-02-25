#include <modes/netradio_mode.h>
#include <esper-gui/views/framework.h>
#include <views/wifi_icon.h>
#include <AudioTools/AudioLibs/AudioSourceLittleFS.h>

using std::make_shared;
using std::shared_ptr;


static const char LOG_TAG[] = "APL_IRA";

const char *urls[] = {
    "https://u1.happyhardcore.com/"
  };


class InternetRadioMode::InternetRadioView: public UI::View {
public:
    shared_ptr<WiFiIcon> wifi;
    shared_ptr<UI::TinySpinner> loading;

    shared_ptr<UI::Label> lblTitle;
    shared_ptr<UI::Label> lblSubtitle;

    InternetRadioView(EGRect f): View(f) {
        wifi = make_shared<WiFiIcon>(WiFiIcon({{0, 32 - 5}, {5, 5}}));
        loading = make_shared<UI::TinySpinner>(UI::TinySpinner({{0, 32 - 5}, {5, 5}}));
        loading->hidden = true;

        lblTitle = make_shared<UI::Label>(UI::Label({{0, 8}, {160, 16}}, Fonts::FallbackWildcard16px, UI::Label::Alignment::Center));
        lblSubtitle = make_shared<UI::Label>(UI::Label({{0, 0}, {160, 8}}, Fonts::FallbackWildcard8px, UI::Label::Alignment::Center));

        lblTitle->auto_scroll = true;
        lblSubtitle->auto_scroll = true;

        subviews.push_back(wifi);
        subviews.push_back(loading);
        subviews.push_back(lblTitle);
        subviews.push_back(lblSubtitle);
    }

    void update_meta(MetaDataType type, const char * str, int len) {
        ESP_LOGI(LOG_TAG, "META['%s'] = '%s'", toStr(type), str);
        if(type == MetaDataType::Title) {
            has_metadata = true;
            lblSubtitle->set_value(station);
            lblTitle->set_value(str);
        }
        else if(type == MetaDataType::Name) {
            // usually just the station ID
            station = str;
            if(has_metadata) {
                lblSubtitle->set_value(station);
            } 
            else {
                lblTitle->set_value(station);
            }
        }
    }

    void reset_meta(const std::string station_name) {
        lblSubtitle->set_value("");
        lblTitle->set_value(station_name);
        station = station_name;
        has_metadata = false;
    }
private:
    bool has_metadata = false; // if true, station title goes on top and track name is the big one
    std::string station = "";
};

InternetRadioMode::InternetRadioMode(const PlatformSharedResources res):
    urlStream(),
    source(urlStream, urls, "audio/mp3"),
    codec(),
    player(),
    sndTask("IRASND", 12000, 15, 1),
    station_buttons({}), // todo
    chgSource(Button(res.keypad, (1 << 0))),
    moreStations(Button(res.keypad, (1 << 7))),
    Mode(res) {
        _that = this;
    rootView = new InternetRadioView({{0, 0}, {160, 32}});
}

void InternetRadioMode::setup() {
    rootView->reset_meta("Radio!");
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
    resources.router->activate_route(Platform::AudioRoute::ROUTE_INTERNAL_CPU);
    player.setAudioSource(source);
    player.setBufferSize(128000);
    player.setDecoder(codec);
    player.setOutput(*resources.router->get_output_port());
    player.addNotifyAudioChange(resources.router->get_output_port());
    player.setMetadataCallback(_update_meta_global);
    player.begin();
    sndTask.begin([this]() { player.copy(); });
}

// cannot use lambda, so have to resort to this hack for now
InternetRadioMode* InternetRadioMode::_that = nullptr;
void InternetRadioMode::_update_meta_global(MetaDataType type, const char * str, int len) {
    if(_that != nullptr) _that->update_meta(type, str, len); 
}

void InternetRadioMode::update_meta(MetaDataType type, const char * str, int len) {
    rootView->update_meta(type, str, len);
}

void InternetRadioMode::loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}

void InternetRadioMode::teardown() {
    sndTask.end();
    resources.router->activate_route(Platform::AudioRoute::ROUTE_NONE_INACTIVE);
}

InternetRadioMode::~InternetRadioMode() {
    _that = nullptr;
    delete rootView;
}

UI::View& InternetRadioMode::main_view() {
    return *rootView;
}