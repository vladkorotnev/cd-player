#include <modes/netradio_mode.h>
#include <esper-gui/views/framework.h>
#include <radio_store.h>
#include <localize.h>
#include "netradio/pipeline.h"
#include "netradio/view.h"

using std::make_shared;
using std::shared_ptr;

static const char LOG_TAG[] = "APL_IRA";

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

    auto url = NetRadio::URLForIndex(index);
    if(!url.empty()) {
        if(url.rfind("http", 0) != 0) {
            url = "http://" + url;
        }

        auto name = NetRadio::NameForIndex(index);
        if(name.empty()) {
            name = url;
        }

        rootView->reset_meta(name);
        play(url);
    } else {
        stop();
        rootView->reset_meta(localized_string("No Preset"));
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
    }

    auto stopBtnSts = stopBtn.get_state();

    if(stopBtnSts == Button::State::BTN_STATE_HELD) {
        ESP_LOGI(LOG_TAG, "Stop buffon held");
        rootView->toggle_diags();
        return;
    }
    else if(stopBtnSts == Button::State::BTN_STATE_CLICKED) {
        select_station(-1);
        return;
    }

    if(now - last_stream_health_check >= pdMS_TO_TICKS(500) && streamer != nullptr) {
        last_stream_health_check = now;
        int health = streamer->getNetBufferHealth();
        rootView->lblWarning->hidden = (health > streamer->getEnoughMark()/2);
        rootView->diagsGroup->set_data(
            health,
            streamer->getNetBufferVolume(),
            streamer->getCodecName(),
            streamer->getEnoughMark(),
            streamer->getBps(),
            streamer->getStreamBitrate()
        );
    }

    delay(10);
}

void InternetRadioMode::on_remote_key_pressed(VirtualKey key) {
    if(RVK_IS_DIGIT(key)) {
        int station_no = RVK_TO_DIGIT(key);
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