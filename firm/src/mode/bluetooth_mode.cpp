#include <modes/bluetooth_mode.h>
#include <esper-core/service.h>
#include <esper-gui/views/framework.h>

using std::make_shared;
using std::shared_ptr;

static const char LOG_TAG[] = "APL_A2DP";
static BluetoothMode * _that = nullptr;

class BluetoothMode::BluetoothView: public UI::View {
public:
    BluetoothView(EGRect f): View(f) {
        lblTitle = make_shared<UI::Label>(UI::Label({{0, 8}, {160, 16}}, Fonts::FallbackWildcard16px, UI::Label::Alignment::Center));
        lblSubtitle = make_shared<UI::Label>(UI::Label({EGPointZero, {160, 8}}, Fonts::FallbackWildcard8px, UI::Label::Alignment::Center));
        lblSource = make_shared<UI::Label>(UI::Label({{0, 24}, {160, 8}}, Fonts::FallbackWildcard8px, UI::Label::Alignment::Right, "Bluetooth"));

        lblTitle->auto_scroll = true;
        lblTitle->synchronize_scrolling_to(&lblSubtitle);
        lblSubtitle->auto_scroll = true;
        lblSubtitle->synchronize_scrolling_to(&lblTitle);
        lblSource->hidden = true;

        subviews.push_back(lblTitle);
        subviews.push_back(lblSubtitle);
        subviews.push_back(lblSource);
    }

    void set_wait() {
        state.connection = WAIT;
        state.artist = "";
        state.title = "";
        update();
    }

    void set_disconnected() {
        state.connection = DISCONNECTED;
        state.artist = "";
        state.title = "";
        update();
    }

    void set_pairing(int pin) {
        state.connection = PAIRING_PIN;
        state.pincode = pin;
        update();
    }

    void set_connected(const char * dev_name) {
        const std::string disp_name = (dev_name == nullptr) ? "" : dev_name;
        if((state.connection != CONNECTED && state.connection != PLAYING) || disp_name != state.device_name) {
            if(state.connection != CONNECTED && state.connection != PLAYING) {
                state.connection = CONNECTED;
            }
            state.device_name = (disp_name.empty() ? "(BT Device)" : disp_name);
            update();
        }
    }

    void update_metadata(const char * text, esp_avrc_md_attr_mask_t id) {
        if(state.connection != CONNECTED && state.connection != PLAYING) return;

        std::string val = (text == nullptr) ? "" : std::string(text);
        if(val == "unavailable" || val == "Not Provided") val = "";

        switch(id) {
            case ESP_AVRC_MD_ATTR_ARTIST:
                state.artist = val;
                break;

            case ESP_AVRC_MD_ATTR_TITLE:
                state.title = val;
                break;

            default:
                ESP_LOGI(LOG_TAG, "Unsupported meta type 0x%02x received: '%s'", id, val.c_str());
                return;
        }

        update();
    }

    void set_playing(bool playing) {
        if(state.connection != CONNECTED && state.connection != PLAYING) return;
        state.connection = playing ? PLAYING : CONNECTED;
        update();
    }

protected:
    shared_ptr<UI::Label> lblTitle;
    shared_ptr<UI::Label> lblSubtitle;
    shared_ptr<UI::Label> lblSource;

    enum ConnectionStatus {
        DISCONNECTED,
        PAIRING_PIN,
        CONNECTED,
        PLAYING,
        WAIT
    };

    struct State {
        ConnectionStatus connection;
        int pincode;
        std::string device_name;
        std::string artist;
        std::string title;
    };

    State state;

    void update() {
        if(state.connection == DISCONNECTED) {
            lblSource->hidden = true;
            lblSubtitle->hidden = true;
            lblTitle->set_value("Bluetooth");
        }
        else if(state.connection == PAIRING_PIN) {
            lblSource->hidden = false;
            lblSubtitle->hidden = false;
            lblSource->set_value(state.device_name);
            lblSubtitle->set_value("Confirm PIN and press PLAY");
            lblTitle->set_value(std::to_string(state.pincode));
        }
        else if(state.connection == CONNECTED) {
            lblSource->hidden = true;
            lblSubtitle->hidden = true;
            lblTitle->set_value(state.device_name);
        }
        else if(state.connection == PLAYING) {
            lblSource->hidden = state.title.empty();
            lblSubtitle->hidden = state.title.empty();
            lblSource->set_value(state.device_name);
            lblTitle->set_value(state.title.empty() ? state.device_name : state.title);
            lblSubtitle->set_value(state.artist);
        }
        else if(state.connection == WAIT) {
            lblSource->hidden = true;
            lblSubtitle->hidden = true;
            lblTitle->set_value("Please Wait");
        }
    }
};

void BluetoothMode::avrc_metadata_callback(uint8_t id, const uint8_t *text) {
    if(_that != nullptr) _that->metadata_callback(id, (const char*) text);
  }

void BluetoothMode::avrc_rn_playstatus_callback(esp_avrc_playback_stat_t playback) {
    if(_that != nullptr) _that->play_status_callback(playback);
}

BluetoothMode::BluetoothMode(const PlatformSharedResources res, ModeHost * host):
    a2dp(*res.router->get_output_port()),
    stopEject(res.keypad, (1 << 0)),
    playPause(res.keypad, (1 << 1)),
    prev(res.keypad, (1 << 4)),
    next(res.keypad, (1 << 5)),
    Mode(res, host) {
        rootView = new BluetoothView({EGPointZero, DISPLAY_SIZE});
        _that = this;
}

void BluetoothMode::setup() {
    rootView->set_disconnected();
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
    resources.router->activate_route(Platform::AudioRoute::ROUTE_INTERNAL_CPU);
    Core::Services::WLAN::stop();
    a2dp.set_rssi_active(true);
    a2dp.set_avrc_metadata_attribute_mask(ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST);
    a2dp.set_avrc_metadata_callback(avrc_metadata_callback);
    a2dp.set_avrc_rn_playstatus_callback(avrc_rn_playstatus_callback);
    a2dp.activate_pin_code(true);
    a2dp.start(("ESPer-CDP_" + Core::Services::WLAN::chip_id()).c_str(), true);
    a2dp.set_discoverability(esp_bt_discovery_mode_t::ESP_BT_GENERAL_DISCOVERABLE);
}

void BluetoothMode::loop() {
    if(a2dp.is_connected()) {
        rootView->set_connected(a2dp.get_peer_name());
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
        else if(stopEject.is_clicked()) a2dp.stop();
    } else {
        if(a2dp.pin_code() != 0) {
            rootView->set_pairing(a2dp.pin_code());

            if(playPause.is_clicked()) {
                rootView->set_wait();
                a2dp.confirm_pin_code();
                delay(1000);
            }
        }
        else {
            rootView->set_disconnected();
        }
    }
    delay(125);
}

void BluetoothMode::on_remote_key_pressed(VirtualKey key) {
    if(a2dp.is_connected()) {
        switch(key) {
            case RVK_PLAY:
                a2dp.play();
                break;

            case RVK_PAUSE:
                a2dp.pause();
                break;

            case RVK_STOP:
                a2dp.stop();
                break;
            
            case RVK_TRACK_NEXT:
                a2dp.next();
                break;

            case RVK_TRACK_PREV:
                a2dp.previous();
                break;

            case RVK_CURS_UP:
                a2dp.set_volume(std::min(127, a2dp.get_volume() + 10));
                break;

            case RVK_CURS_DOWN:
                a2dp.set_volume(std::max(0, a2dp.get_volume() - 10));
                break;

            case RVK_SEEK_BACK:
                a2dp.rewind();
                break;

            case RVK_SEEK_FWD:
                a2dp.fast_forward();
                break;

            default: break;
        }
    }
    else if(a2dp.pin_code() != 0 && (key == RVK_PLAY || key == RVK_CURS_ENTER)) {
        rootView->set_wait();
        a2dp.confirm_pin_code();
        delay(1000);
    }
}

void BluetoothMode::play_status_callback(esp_avrc_playback_stat_t sts) {
    cur_sts = sts;
    rootView->set_playing(cur_sts == ESP_AVRC_PLAYBACK_PLAYING || cur_sts == ESP_AVRC_PLAYBACK_FWD_SEEK || cur_sts == ESP_AVRC_PLAYBACK_REV_SEEK);
}

void BluetoothMode::metadata_callback(uint8_t id, const char *text) {
    rootView->update_metadata(text, (esp_avrc_md_attr_mask_t) id);
}

void BluetoothMode::teardown() {
    rootView->set_wait();

    if(a2dp.is_connected()) {
        a2dp.disconnect();
    }
    a2dp.end();
    resources.router->activate_route(Platform::AudioRoute::ROUTE_NONE_INACTIVE);

    // a2dp.end(true) would have called those for us but went too far and actually deinited the controller, we want to just stop it until we come back into this mode again
    esp_bluedroid_disable();
    esp_bt_controller_disable();

    Core::Services::WLAN::start();
}

BluetoothMode::~BluetoothMode() {
    _that = nullptr;
    delete rootView;
}

UI::View& BluetoothMode::main_view() {
    return *rootView;
}