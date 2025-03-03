#include <modes/netradio_mode.h>
#include <esper-gui/views/framework.h>
#include <views/wifi_icon.h>
#include "netradio/pipeline.h"

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
                EGDrawLine(buf, {line_left_cur, lbl->frame.origin.y + lbl->frame.size.height + 1}, {line_right_cur, lbl->frame.origin.y + lbl->frame.size.height + 1});
            }
        }
        View::render(buf);
    }

    bool needs_display() override {
        if(line_right_tgt != line_right_cur || line_left_tgt != line_left_cur) {
            bool going_right = (line_right_tgt > line_right_cur || line_left_tgt > line_left_cur);
            int* first_coord = going_right ? &line_right_cur : &line_left_cur;
            int* first_coord_tgt = going_right ? &line_right_tgt : &line_left_tgt;
            int* second_coord = !going_right ? &line_right_cur : &line_left_cur;
            int* second_coord_tgt = !going_right ? &line_right_tgt : &line_left_tgt;

            if(*first_coord_tgt > *first_coord) {
                *first_coord += std::min(line_speed/2, *first_coord_tgt - *first_coord);
                line_speed = std::min(32, line_speed+1);
            }
            else if(*first_coord_tgt < *first_coord) {
                *first_coord -= std::min(line_speed/2, *first_coord - *first_coord_tgt);
                line_speed = std::min(32, line_speed+1);
            }
            else if(*second_coord_tgt > *second_coord) {
                *second_coord += std::min(line_speed/2, *second_coord_tgt - *second_coord);
                line_speed = std::max(2, line_speed - 1);
            }
            else if(*second_coord_tgt < *second_coord) {
                *second_coord -= std::min(line_speed/2, *second_coord - *second_coord_tgt);
                line_speed = std::max(2, line_speed - 1);
            }

            set_needs_display();
        }
        return View::needs_display();
    }

    void set_active_ch_idx(int num) {
        active_ch_idx = num;
        if(num < 0 || num > subviews.size()) {
            line_left_cur = 0;
            line_right_cur = 0;
            line_left_tgt = 0;
            line_right_tgt = 0;
            set_needs_display();
            return;
        }

        if(line_left_cur == line_left_tgt && line_right_cur == line_right_tgt) line_speed = 2;
        auto lbl = subviews[num];
        if(line_left_cur == 0) line_left_cur = lbl->frame.origin.x - 10;
        if(line_right_cur == 0) line_right_cur = lbl->frame.origin.x - 9;
        line_left_tgt = lbl->frame.origin.x - 1;
        line_right_tgt = lbl->frame.origin.x + lbl->frame.size.width + 1;
        set_needs_display();
    }

private:
    int active_ch_idx = -1;
    int line_speed = 2;
    
    int line_left_cur = 0;
    int line_right_cur = 0;
    int line_left_tgt = 0;
    int line_right_tgt = 0;
};

class InternetRadioMode::InternetRadioView: public UI::View {
public:
    shared_ptr<WiFiIcon> wifi;
    shared_ptr<UI::TinySpinner> loading;
    shared_ptr<UI::Label> lblWarning;

    shared_ptr<UI::Label> lblTitle;
    shared_ptr<UI::Label> lblSubtitle;

    shared_ptr<ChannelGridBar> channelBar;

    InternetRadioView(EGRect f): View(f) {
        wifi = make_shared<WiFiIcon>(WiFiIcon({{0, 32 - 7}, {5, 5}}));
        loading = make_shared<UI::TinySpinner>(UI::TinySpinner({{160 - 6, 32 - 7}, {5, 5}}));
        loading->hidden = true;

        channelBar = make_shared<ChannelGridBar>(ChannelGridBar({{5, 32 - 7}, {160 - 12, 7}}));

        lblTitle = make_shared<UI::Label>(UI::Label({{0, 8}, {160, 16}}, Fonts::FallbackWildcard16px, UI::Label::Alignment::Center));
        lblSubtitle = make_shared<UI::Label>(UI::Label({EGPointZero, {160, 8}}, Fonts::FallbackWildcard8px, UI::Label::Alignment::Center));

        lblWarning = make_shared<UI::Label>(UI::Label({{160 - 6, 32 - 7}, {5, 5}}, Fonts::TinyDigitFont, UI::Label::Alignment::Center, "!"));
        lblWarning->hidden = true;

        lblTitle->auto_scroll = true;
        lblTitle->synchronize_scrolling_to(&lblSubtitle);
        lblSubtitle->auto_scroll = true;
        lblSubtitle->synchronize_scrolling_to(&lblTitle);

        subviews.push_back(wifi);
        subviews.push_back(channelBar);
        subviews.push_back(lblWarning);
        subviews.push_back(loading);
        subviews.push_back(lblTitle);
        subviews.push_back(lblSubtitle);
    }

    void update_meta(MetaDataType type, const char * str, int len) {
        if(type == MetaDataType::Title) {
            if(len > 0) {
                has_metadata = true;
                lblSubtitle->set_value(station);
                lblTitle->set_value(str);
            } else if(has_metadata) {
                has_metadata = false;
                lblSubtitle->set_value("");
                lblTitle->set_value(station);
            }
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


InternetRadioMode::InternetRadioMode(const PlatformSharedResources res, ModeHost * host):
    streamer(nullptr),
    station_buttons({}),
    stopBtn(Button(res.keypad, (1 << 0))),
    Mode(res, host) {
        _that = this;
        rootView = new InternetRadioView({EGPointZero, DISPLAY_SIZE});

        for(int i = 1; i <= 6; i++) {
            station_buttons.push_back(Button(res.keypad, (1 << i)));
        }
}

void InternetRadioMode::setup() {
    rootView->reset_meta("");
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
    resources.router->activate_route(Platform::AudioRoute::ROUTE_INTERNAL_CPU);
}

void InternetRadioMode::select_station(int index) {
    cur_station_index = index;

    if(index <= -1) {
        stop();
        rootView->lblWarning->hidden = true;
        rootView->channelBar->set_active_ch_idx(-1);
        rootView->reset_meta("");
        rootView->set_loading(false);
        return;
    }

    // TODO: database
    switch(index) {
        case 0: rootView->reset_meta("Happy Hardcore"); play("http://u1.happyhardcore.com/"); break;
        case 1: rootView->reset_meta("NDR 1"); play("http://icecast.ndr.de/ndr/ndr1wellenord/kiel/mp3/128/stream.mp3"); break;
        case 2: rootView->reset_meta("Радио Проводач"); play("http://station.waveradio.org/provodach"); break;
        case 3: rootView->reset_meta("Проводач (MP3)");play("http://station.waveradio.org/provodach.mp3"); break;
        case 4: rootView->reset_meta("Советская Волна");play("http://station.waveradio.org/soviet"); break;;
        case 5: rootView->reset_meta("Relax FM");play("http://srv01.gpmradio.ru/stream/reg/mp3/128/region_relax_86"); break;

        default: return;
    }
    rootView->channelBar->set_active_ch_idx(index);
}

// cannot use lambda, so have to resort to this hack for now
InternetRadioMode* InternetRadioMode::_that = nullptr;
void InternetRadioMode::_update_meta_global(MetaDataType type, const char * str, int len) {
    ESP_LOGI(LOG_TAG, "META['%s'] = '%s'", toStr(type), str);
    if(_that != nullptr) _that->update_meta(type, str, len); 
}

void InternetRadioMode::update_meta(MetaDataType type, const char * str, int len) {
    rootView->update_meta(type, str, len);
}

void InternetRadioMode::play(const std::string url) {
    rootView->set_loading(true);
    stop();
    ESP_LOGI(LOG_TAG, "Create new streamer");
    streamer = new StreamingPipeline(resources.router);
    ESP_LOGI(LOG_TAG, "Start new streamer");
    last_stream_health_check = xTaskGetTickCount();
    streamer->start(
        url,
        [this](bool isLoading) { rootView->set_loading(isLoading); }
    );
}

void InternetRadioMode::stop() {
    if(streamer != nullptr) {
        ESP_LOGI(LOG_TAG, "Delete old streamer");
        streamer->stop();
        delete streamer;
        streamer = nullptr;
    }
}

void InternetRadioMode::loop() {
    TickType_t now = xTaskGetTickCount();

    for(int i = 0; i < 6; i++) {
        if(station_buttons[i].is_clicked()) {
            select_station(i);
            return;
        }
        else if(station_buttons[i].is_held()) {
            // TODO: settings?
        }
    }

    if(stopBtn.is_clicked()) {
        select_station(-1);
        return;
    }

    if(now - last_stream_health_check >= pdMS_TO_TICKS(500) && streamer != nullptr) {
        last_stream_health_check = now;
        int health = streamer->getNetBufferHealth();
        rootView->lblWarning->hidden = (health > 30);
    }

    delay(100);
}

void InternetRadioMode::on_remote_key_pressed(VirtualKey key) {
    if(key >= RVK_START_OF_NUMBERS && key < RVK_END_OF_NUMBERS) {
        int station_no = (key - RVK_START_OF_NUMBERS);
        if(station_no > 0 && station_no <= 6)
            select_station(station_no - 1);
        return;
    }

    switch(key) {
        case RVK_SHUFFLE:
            select_station(esp_random() % 6);
            break;

        case RVK_STOP:
            select_station(-1);
            break;

        case RVK_DISK_PREV:
        case RVK_TRACK_PREV:
        case RVK_CURS_LEFT:
            select_station((cur_station_index == 0) ? 5 : (cur_station_index - 1));
            break;

        case RVK_DISK_NEXT:
        case RVK_TRACK_NEXT:
        case RVK_CURS_RIGHT:
            select_station((cur_station_index + 1) % 6);
            break;

        default: break;
    }
}

void InternetRadioMode::teardown() {
    if(streamer != nullptr) {
        streamer->stop();
        delete streamer;
    }

    streamer = nullptr;
    resources.router->activate_route(Platform::AudioRoute::ROUTE_NONE_INACTIVE);
}

InternetRadioMode::~InternetRadioMode() {
    _that = nullptr;
    delete rootView;
}

UI::View& InternetRadioMode::main_view() {
    return *rootView;
}