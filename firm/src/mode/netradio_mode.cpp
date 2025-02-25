#include <modes/netradio_mode.h>
#include <esper-gui/views/framework.h>
#include <views/wifi_icon.h>

using std::make_shared;
using std::shared_ptr;


static const char LOG_TAG[] = "APL_IRA";

class ChannelGridBar: public UI::View {
public:
    ChannelGridBar(EGRect f, int channel_count = 6): View(f) {
        auto font = Fonts::TinyDigitFont;
        int channel_width = frame.size.width / channel_count;
        int channel_margin = channel_width/2 - font->size.width/2; // assuming one digit

        for(int i = 1; i <= channel_count; i++) {
            auto lbl = make_shared<UI::Label>(UI::Label({{(i-1)*channel_width + channel_margin, 0}, {font->size.width, font->size.height}}, font, UI::Label::Alignment::Center, std::to_string(i)));
            subviews.push_back(lbl);
        }
    }

    void render(EGGraphBuf * buf) override {
        for(int i = 0; i < subviews.size(); i++) {
            auto lbl = subviews[i];
            if(i == active_ch_idx) {
                EGDrawLine(buf, {lbl->frame.origin.x - 1, lbl->frame.origin.y + lbl->frame.size.height + 1}, {lbl->frame.origin.x + lbl->frame.size.width + 1, lbl->frame.origin.y + lbl->frame.size.height + 1});
            }
        }
        View::render(buf);
    }

    void set_active_ch_num(int num) {
        active_ch_idx = num - 1;
        set_needs_display();
    }

private:
    int active_ch_idx = -1;
};

class InternetRadioMode::InternetRadioView: public UI::View {
public:
    shared_ptr<WiFiIcon> wifi;
    shared_ptr<UI::TinySpinner> loading;

    shared_ptr<UI::Label> lblTitle;
    shared_ptr<UI::Label> lblSubtitle;

    shared_ptr<ChannelGridBar> channelBar;

    InternetRadioView(EGRect f): View(f) {
        wifi = make_shared<WiFiIcon>(WiFiIcon({{0, 32 - 7}, {5, 5}}));
        loading = make_shared<UI::TinySpinner>(UI::TinySpinner({{160 - 6, 32 - 7}, {5, 5}}));
        loading->hidden = true;

        channelBar = make_shared<ChannelGridBar>(ChannelGridBar({{5, 32 - 7}, {160 - 5, 7}}));
        channelBar->set_active_ch_num(3);

        lblTitle = make_shared<UI::Label>(UI::Label({{0, 8}, {160, 16}}, Fonts::FallbackWildcard16px, UI::Label::Alignment::Center));
        lblSubtitle = make_shared<UI::Label>(UI::Label({{0, 0}, {160, 8}}, Fonts::FallbackWildcard8px, UI::Label::Alignment::Center));

        lblTitle->auto_scroll = true;
        lblSubtitle->auto_scroll = true;

        subviews.push_back(wifi);
        subviews.push_back(loading);
        subviews.push_back(channelBar);
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

    void set_loading(bool isLoading) {
        loading->hidden = !isLoading;
    }
private:
    bool has_metadata = false; // if true, station title goes on top and track name is the big one
    std::string station = "";
};

InternetRadioMode::InternetRadioMode(const PlatformSharedResources res):
    urlStream(128 * 1024),
    mp3(),
    decoder(res.router->get_output_port(), &mp3),
    copier(decoder, urlStream),
    sndTask(nullptr),
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
    
    play();
}

// cannot use lambda, so have to resort to this hack for now
InternetRadioMode* InternetRadioMode::_that = nullptr;
void InternetRadioMode::_update_meta_global(MetaDataType type, const char * str, int len) {
    if(_that != nullptr) _that->update_meta(type, str, len); 
}

void InternetRadioMode::update_meta(MetaDataType type, const char * str, int len) {
    rootView->update_meta(type, str, len);
}

void InternetRadioMode::play() {
    rootView->set_loading(true);
    if(sndTask != nullptr) {
        delete sndTask;
        sndTask = nullptr;
    }
    sndTask = new Task("IRASND", 12000, 15, 1);
    sndTask->begin([this]() { 
        decoder.addNotifyAudioChange(*resources.router->get_output_port());
        urlStream.setMetadataCallback(_update_meta_global);
        copier.resize(128000);
        
        decoder.begin();
        urlStream.begin("http://srv01.gpmradio.ru/stream/reg/mp3/128/region_relax_86", "audio/mpeg");

        rootView->set_loading(false);
        while(1) {
            copier.copy(); 
        }
    });
}

void InternetRadioMode::loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}

void InternetRadioMode::teardown() {
    sndTask->end();
    delete sndTask;
    sndTask = nullptr;
    resources.router->activate_route(Platform::AudioRoute::ROUTE_NONE_INACTIVE);
}

InternetRadioMode::~InternetRadioMode() {
    _that = nullptr;
    delete rootView;
}

UI::View& InternetRadioMode::main_view() {
    return *rootView;
}