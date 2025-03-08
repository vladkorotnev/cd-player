#pragma once
#include "mode.h"
#include "modes/boot_mode.h"
#include "shared_prefs.h"
#include <esper-gui/views/framework.h>
#include <platform.h>
#include <modes/cd_mode.h>
#include <modes/netradio_mode.h>
#include <modes/bluetooth_mode.h>
#include <modes/settings_mode.h>
#include <modes/standby_mode.h>

enum ModeSelection: int {
    ESPER_MODE_CD,
    ESPER_MODE_NET_RADIO,
    ESPER_MODE_BLUETOOTH,
    ESPER_MODE_SETTINGS,
    ESPER_MODE_STANDBY,

    ESPER_MODE_MAX_INVALID
};

class ModeHost {
public:
    ModeHost(const PlatformSharedResources res, int uiThreadAffinity = 0, int uiThreadPriority = 2):
        resources(res),
        compositor(res.display),
        modeSw(res.keypad, (1 << 7)) {
            modeSwitchSema = xSemaphoreCreateBinary();
            activeMode = new BootMode(res, this);
            xSemaphoreGive(modeSwitchSema);
            
            resources.display->set_brightness((Graphics::Hardware::Brightness) Prefs::get(PREFS_KEY_CUR_BRIGHTNESS));

            xTaskCreatePinnedToCore(
                host_task,
                "APPHOST",
                16000,
                this,
                uiThreadPriority,
                &hostTaskHandle,
                uiThreadAffinity
            );
    }

    ~ModeHost() {
        if(hostTaskHandle != NULL) vTaskDelete(hostTaskHandle);
        if(modeSwitchSema != NULL) vSemaphoreDelete(modeSwitchSema);
    }

    void activate_mode(ModeSelection sel) {
        req_mode = sel;
    }

    void activate_last_used_mode() {
        auto last_mode = (ModeSelection) Prefs::get(PREFS_KEY_CUR_MODE);
        if(last_mode != ESPER_MODE_CD) {
            resources.cdrom->start(false); // power down the cd drive if not going to CD mode
        }
        activate_mode(last_mode);
    }

    void reinit_current_mode() {
        req_mode = cur_mode;
        cur_mode = ESPER_MODE_MAX_INVALID;
    }

    void loop() {
        xSemaphoreTake(modeSwitchSema, portMAX_DELAY);
        if(activeMode != nullptr) {
            activeMode->loop();
        }

        if(cur_mode != ESPER_MODE_MAX_INVALID) {
            // Controls inert until booted
            if(resources.remote->has_new_code()) {
                auto code = resources.remote->code();
                if(code == RVK_MODE_CD) {
                    activate_mode(ESPER_MODE_CD);
                }
                else if(code == RVK_MODE_RADIO) {
                    activate_mode(ESPER_MODE_NET_RADIO);
                }
                else if(code == RVK_MODE_BLUETOOTH) {
                    activate_mode(ESPER_MODE_BLUETOOTH);
                }
                else if(code == RVK_MODE_STANDBY) {
                    if(cur_mode == ESPER_MODE_STANDBY) {
                        activate_last_used_mode();
                    }
                    else {
                        activate_mode(ESPER_MODE_STANDBY);
                    }
                }
                else if(code == RVK_MODE_SWITCH) {
                    activate_next_mode();
                }
                else if(code == RVK_MODE_SETTINGS && cur_mode != ESPER_MODE_STANDBY) {
                    activate_mode(ESPER_MODE_SETTINGS);
                }
                else if(code == RVK_DIMMER && cur_mode != ESPER_MODE_STANDBY) {
                    Graphics::Hardware::Brightness brightness = resources.display->get_brightness();
                    if(brightness == Graphics::Hardware::Brightness::DISP_BRIGHTNESS_75) {
                        brightness = Graphics::Hardware::DISP_BRIGHTNESS_25;
                    }
                    else if(brightness == Graphics::Hardware::Brightness::DISP_BRIGHTNESS_25) {
                        brightness = Graphics::Hardware::DISP_BRIGHTNESS_0;
                    }
                    else {
                        brightness = Graphics::Hardware::DISP_BRIGHTNESS_75;
                    }
                    
                    ESP_LOGD(LOG_TAG, "Brightness = %i -> %i", (int) resources.display->get_brightness(), (int) brightness);
                    resources.display->set_brightness(brightness);
    
                    // don't save 0 into the nvram to avoid confusion on next power on
                    if(brightness != Graphics::Hardware::Brightness::DISP_BRIGHTNESS_0)
                        Prefs::set(PREFS_KEY_CUR_BRIGHTNESS, (int) brightness);
                }
                else if(code == RVK_EJECT && cur_mode != ESPER_MODE_CD && cur_mode != ESPER_MODE_STANDBY) {
                    // eject when not in CD mode is handled by us
                    auto cd = resources.cdrom;
                    cd->eject(cd->check_media() != ATAPI::MediaTypeCode::MTC_DOOR_OPEN);
                }
                else if(activeMode != nullptr) {
                    activeMode->on_remote_key_pressed((VirtualKey)code);
                }
            }
            else if(modeSw.is_clicked()) {
                activate_next_mode();
            }
            else if(modeSw.is_held_continuously() && cur_mode != ESPER_MODE_STANDBY) {
                activate_mode(ESPER_MODE_STANDBY);
            }
        }
        
        if(cur_mode != req_mode) {
            switch_to_req_mode_locked();
        }
        xSemaphoreGive(modeSwitchSema);
    }

private:
    const char * LOG_TAG = "APPHOST";
    const Prefs::Key<int> PREFS_KEY_CUR_MODE {"last_mode", (int)ESPER_MODE_SETTINGS};
    const Prefs::Key<int> PREFS_KEY_CUR_BRIGHTNESS {"last_dimmer", (int)Graphics::Hardware::Brightness::DISP_BRIGHTNESS_75};
    PlatformSharedResources resources;
    Graphics::Compositor compositor;
    TaskHandle_t hostTaskHandle = NULL;
    Mode * activeMode = nullptr;
    SemaphoreHandle_t modeSwitchSema = NULL;
    ModeSelection cur_mode = ESPER_MODE_MAX_INVALID;
    ModeSelection req_mode = ESPER_MODE_MAX_INVALID;
    Button modeSw;

    void activate_next_mode() {
        ModeSelection m;
        if(cur_mode == ESPER_MODE_STANDBY) {
            m = (ModeSelection) Prefs::get(PREFS_KEY_CUR_MODE);
        }
        else if(cur_mode == ESPER_MODE_CD) {
            if(Prefs::get(PREFS_KEY_RADIO_MODE_INCLUDED)) {
                m = ESPER_MODE_NET_RADIO;
            }
            else if(Prefs::get(PREFS_KEY_BLUETOOTH_MODE_INCLUDED)) {
                m = ESPER_MODE_BLUETOOTH;
            }
            else {
                // nowhere to go
                return;
            }
        }
        else if(cur_mode == ESPER_MODE_NET_RADIO) {
            if(Prefs::get(PREFS_KEY_BLUETOOTH_MODE_INCLUDED)) {
                m = ESPER_MODE_BLUETOOTH;
            }
            else if(Prefs::get(PREFS_KEY_CD_MODE_INCLUDED)) {
                m = ESPER_MODE_CD;
            }
            else {
                // nowhere to go
                return;
            }
        }
        else {
            if(Prefs::get(PREFS_KEY_CD_MODE_INCLUDED)) {
                m = ESPER_MODE_CD;
            }
            else if(Prefs::get(PREFS_KEY_RADIO_MODE_INCLUDED)) {
                m = ESPER_MODE_NET_RADIO;
            }
            else {
                // nowhere to go
                return;
            }
        }
        activate_mode(m);
    }

    void switch_to_req_mode_locked() {
        if(req_mode == cur_mode) return;

        if(activeMode != nullptr) {
            activeMode->teardown();
            delete activeMode;
            activeMode = nullptr;
        }

        switch(req_mode) {
            case ESPER_MODE_CD:
                activeMode = new CDMode(resources, this);
                break;

            case ESPER_MODE_BLUETOOTH:
                activeMode = new BluetoothMode(resources, this);
                break;

            case ESPER_MODE_NET_RADIO:
                activeMode = new InternetRadioMode(resources, this);
                break;

            case ESPER_MODE_SETTINGS:
                activeMode = new SettingsMode(resources, this);
                break;

            case ESPER_MODE_STANDBY:
                activeMode = new StandbyMode(resources, this);
                break;
            default:
                ESP_LOGE(LOG_TAG, "Unsupported mode ID 0x%02x", req_mode);
                break;
        }

        if(activeMode != nullptr) {
            activeMode->setup();
        }

        cur_mode = req_mode;
        if(cur_mode != ESPER_MODE_STANDBY)
            Prefs::set(PREFS_KEY_CUR_MODE, (int) cur_mode);
    }

    void composition_pass() {
        if(activeMode != nullptr) {
            compositor.render(activeMode->main_view());
        }
    }

    static void host_task(void * p) {
        ModeHost * that = static_cast<ModeHost *>(p);
        while(1) {
            that->composition_pass();
            vTaskDelay(pdMS_TO_TICKS(33));
        }
        vTaskDelete(NULL);
    }
};