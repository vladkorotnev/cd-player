#pragma once
#include "mode.h"
#include <esper-gui/views/framework.h>
#include <platform.h>
#include <modes/cd_mode.h>
#include <modes/netradio_mode.h>
#include <modes/bluetooth_mode.h>

enum ModeSelection {
    ESPER_MODE_CD,
    // ESPER_MODE_NET_RADIO,
    ESPER_MODE_BLUETOOTH,

    ESPER_MODE_MAX_INVALID
};

class ModeHost {
public:
    ModeHost(const PlatformSharedResources res, int uiThreadAffinity = 0, int uiThreadPriority = 2):
        resources(res),
        compositor(res.display),
        modeSw(res.keypad, (1 << 7)) {
            modeSwitchSema = xSemaphoreCreateBinary();
            xSemaphoreGive(modeSwitchSema);
            
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

    void loop() {
        xSemaphoreTake(modeSwitchSema, portMAX_DELAY);
        if(activeMode != nullptr) {
            activeMode->loop();
        }
        if(modeSw.is_clicked()) {
            ModeSelection m = (ModeSelection) (((int)cur_mode + 1) % ESPER_MODE_MAX_INVALID);
            activate_mode(m);
        }
        if(cur_mode != req_mode) {
            switch_to_req_mode_locked();
        }
        xSemaphoreGive(modeSwitchSema);
    }

private:
    const char * LOG_TAG = "APPHOST";
    PlatformSharedResources resources;
    Graphics::Hardware::DisplayDriver * display;
    Graphics::Compositor compositor;
    TaskHandle_t hostTaskHandle = NULL;
    Mode * activeMode = nullptr;
    SemaphoreHandle_t modeSwitchSema = NULL;
    ModeSelection cur_mode = ESPER_MODE_MAX_INVALID;
    ModeSelection req_mode = ESPER_MODE_MAX_INVALID;
    Button modeSw;

    void switch_to_req_mode_locked() {
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

            // case ESPER_MODE_NET_RADIO:
            //     activeMode = new InternetRadioMode(resources, this);
            //     break;

            default:
                ESP_LOGE(LOG_TAG, "Unsupported mode ID 0x%02x", req_mode);
                break;
        }

        if(activeMode != nullptr) {
            activeMode->setup();
        }

        cur_mode = req_mode;
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