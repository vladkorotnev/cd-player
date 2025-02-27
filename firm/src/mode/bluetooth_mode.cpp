#include <modes/bluetooth_mode.h>
#include <esper-core/service.h>
#include <esper-gui/views/framework.h>

using std::make_shared;
using std::shared_ptr;

static const char LOG_TAG[] = "APL_A2DP";
static BluetoothMode * _that = nullptr;

class BluetoothMode::BluetoothView: public UI::View {
public:
    shared_ptr<UI::TinySpinner> loading;
    shared_ptr<UI::Label> lblTitle;
    shared_ptr<UI::Label> lblSubtitle;


    BluetoothView(EGRect f): View(f) {
        lblTitle = make_shared<UI::Label>(UI::Label({{0, 8}, {160, 16}}, Fonts::FallbackWildcard16px, UI::Label::Alignment::Center));
        lblSubtitle = make_shared<UI::Label>(UI::Label({{0, 0}, {160, 8}}, Fonts::FallbackWildcard8px, UI::Label::Alignment::Center));

        loading = make_shared<UI::TinySpinner>(UI::TinySpinner({{160 - 6, 32 - 7}, {5, 5}}));
        loading->hidden = true;

        lblTitle->auto_scroll = true;
        lblTitle->synchronize_scrolling_to(&lblSubtitle);
        lblSubtitle->auto_scroll = true;
        lblSubtitle->synchronize_scrolling_to(&lblTitle);

        subviews.push_back(lblTitle);
        subviews.push_back(lblSubtitle);
        subviews.push_back(loading);
    }

    void set_loading(bool isLoading) {
        loading->hidden = !isLoading;
    }
};

void BluetoothMode::avrc_metadata_callback(uint8_t id, const uint8_t *text) {
    if(_that != nullptr) _that->metadata_callback(id, (const char*) text);
  }

void BluetoothMode::avrc_rn_playstatus_callback(esp_avrc_playback_stat_t playback) {
    if(_that != nullptr) _that->play_status_callback(playback);
}

BluetoothMode::BluetoothMode(const PlatformSharedResources res):
    a2dp(*res.router->get_output_port()),
    playPause(res.keypad, (1 << 1)),
    prev(res.keypad, (1 << 4)),
    next(res.keypad, (1 << 5)),
    Mode(res) {
        rootView = new BluetoothView({{0, 0}, {160, 32}});
        _that = this;
}

void BluetoothMode::setup() {
    rootView->lblTitle->set_value("Bluetooth");
    rootView->lblSubtitle->set_value("");
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
    resources.router->activate_route(Platform::AudioRoute::ROUTE_INTERNAL_CPU);
    Core::Services::WLAN::stop();
    a2dp.set_rssi_active(true);
    a2dp.set_avrc_metadata_attribute_mask(ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST);
    a2dp.set_avrc_metadata_callback(avrc_metadata_callback);
    a2dp.set_avrc_rn_playstatus_callback(avrc_rn_playstatus_callback);
    a2dp.activate_pin_code(true);
    a2dp.start("ESPer-CDP", true);
    a2dp.set_discoverability(esp_bt_discovery_mode_t::ESP_BT_GENERAL_DISCOVERABLE);
}

void BluetoothMode::loop() {
    if(a2dp.is_connected()) {
        if(!was_connected) {
            rootView->lblTitle->set_value(a2dp.get_peer_name());
        }
        was_connected = true;
        if(playPause.is_clicked()) {
            if(cur_sts == ESP_AVRC_PLAYBACK_PLAYING || cur_sts == ESP_AVRC_PLAYBACK_FWD_SEEK || cur_sts == ESP_AVRC_PLAYBACK_REV_SEEK) {
                a2dp.pause();
            }
            else {
                a2dp.play();
            }
        }
        else if(next.is_clicked()) a2dp.next();
        else if(prev.is_clicked()) a2dp.previous();
    } else {
        if(a2dp.pin_code() != 0) {
            rootView->lblSubtitle->set_value("Confirm PIN matches and press PLAY if so");
            rootView->lblTitle->set_value(std::to_string(a2dp.pin_code()));

            if(playPause.is_clicked()) {
                rootView->lblSubtitle->set_value("");
                rootView->lblTitle->set_value("Pairing...");
                a2dp.confirm_pin_code();
            }
        }
        else if(was_connected) {
            rootView->lblTitle->set_value("Bluetooth");
            rootView->lblSubtitle->set_value("");
            was_connected = false;
        }
    }
    delay(125);
}

void BluetoothMode::play_status_callback(esp_avrc_playback_stat_t sts) {
    cur_sts = sts;
    if(cur_sts == ESP_AVRC_PLAYBACK_PLAYING || cur_sts == ESP_AVRC_PLAYBACK_FWD_SEEK || cur_sts == ESP_AVRC_PLAYBACK_REV_SEEK) {
        rootView->lblTitle->set_value(cur_title);
        rootView->lblSubtitle->set_value(cur_artist);
    }
}

void BluetoothMode::metadata_callback(uint8_t id, const char *text) {
    std::string val = (text == nullptr) ? "" : std::string(text);
    if(val == "unavailable" || val == "Not Provided") val = "";

    switch(id) {
        case ESP_AVRC_MD_ATTR_ARTIST:
            cur_artist = val;
            break;

        case ESP_AVRC_MD_ATTR_TITLE:
            cur_title = val;
            break;

        default:
            ESP_LOGI(LOG_TAG, "Unsupported meta type 0x%02x received: '%s'", id, val.c_str());
            break;
    }

    rootView->lblSubtitle->set_value(cur_artist);
    if(cur_title != "") {
        rootView->lblTitle->set_value(cur_title);
    } else {
        rootView->lblTitle->set_value(a2dp.get_peer_name());
    }
}

void BluetoothMode::teardown() {
    if(a2dp.is_connected()) {
        a2dp.disconnect();
    }
    a2dp.end();
    resources.router->activate_route(Platform::AudioRoute::ROUTE_NONE_INACTIVE);
    Core::Services::WLAN::start();
}

BluetoothMode::~BluetoothMode() {
    _that = nullptr;
}

UI::View& BluetoothMode::main_view() {
    return *rootView;
}