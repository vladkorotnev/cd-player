#include <otafvu.h>
#include <esper-core/wlan.h>
#include <ArduinoOTA.h>
#include <consts.h>
#include <shared_prefs.h>

static char LOG_TAG[] = "OTAFVU";

#ifdef OTA_FVU_ENABLED
static TaskHandle_t hTask = NULL;

class OTAFVUUI: public UI::View {
public:
    std::shared_ptr<UI::Label> stsLabel;
    std::shared_ptr<UI::ProgressBar> progBar;
    OTAFVUUI(): View(EGRect {EGPointZero, {160, 32}}),
        stsLabel(std::make_shared<UI::Label>(EGRect {{0, 8}, {frame.size.width, 16}}, Fonts::FallbackWildcard16px, UI::Label::Alignment::Center, "OTA FVU")),
        progBar(std::make_shared<UI::ProgressBar>(EGRect {{0, 24}, {frame.size.width, 8}}))
     {
        subviews = {
            stsLabel,
            progBar
        };
     }
};

class OTAFVUUIMode: public Mode {
public:
    OTAFVUUIMode(const PlatformSharedResources res, ModeHost * host): Mode(res, host) {}

    void on_progress(unsigned int progress, unsigned int total) {
        ui.progBar->maximum = total;
        ui.progBar->value = progress;
        ui.progBar->set_needs_display();
    }

    void on_end() {
        ui.progBar->hidden = true;
        ui.stsLabel->set_value("FVU Completed");
    }

    void on_error(ota_error_t error) {
        ui.progBar->hidden = true;
        switch(error) {
            case OTA_AUTH_ERROR:
                ui.stsLabel->set_value("Auth error");
                break;

            case OTA_BEGIN_ERROR:
                ui.stsLabel->set_value("Begin error");
                break;

            case OTA_CONNECT_ERROR:
                ui.stsLabel->set_value("Connect error");
                break;

            case OTA_RECEIVE_ERROR:
                ui.stsLabel->set_value("Receive error");
                break;

            case OTA_END_ERROR:
                ui.stsLabel->set_value("End error");
                break;

            default:
                ui.stsLabel->set_value("Unknown error");
                break;
        }
    }

    void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }

    UI::View& main_view() override { return ui; }
protected:
    OTAFVUUI ui;
};

static OTAFVUUIMode * mode = nullptr;

static void OtaFvuTaskFunction( void * pvParameter )
{
    ESP_LOGV(LOG_TAG, "Running task");
    while(1) {
        ArduinoOTA.handle();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
#endif

namespace OTAFVU {
    void start_if_needed(const PlatformSharedResources res, ModeHost * host) {
#ifdef OTA_FVU_ENABLED

    if(!Prefs::get(PREFS_KEY_OTAFVU_ALLOWED)) {
        ESP_LOGW(LOG_TAG, "OTAFVU code present but disabled by user");
        return;
    }

    ArduinoOTA.setHostname((std::string("esper-") + Core::Services::WLAN::chip_id()).c_str());
#ifndef OTAFVU_PORT
#define OTAFVU_PORT 3232
#endif
    ArduinoOTA.setPort(OTAFVU_PORT);
    ArduinoOTA.setPasswordHash(OTA_FVU_PASSWORD_HASH);
    ArduinoOTA.setRebootOnSuccess(false);


    ArduinoOTA.onStart([res, host] {
        mode = new OTAFVUUIMode(res, host);
        host->set_external_mode(mode);
    });

    ArduinoOTA.onProgress([] (unsigned int got, unsigned int of) { 
        mode->on_progress(got, of); 
    });
    ArduinoOTA.onError([] (ota_error_t error) { 
        mode->on_error(error);
        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP.restart();
    });
    ArduinoOTA.onEnd([] { 
        mode->on_end();
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP.restart();
    });

    ArduinoOTA.begin();

    ESP_LOGV(LOG_TAG, "Creating task");
    if(xTaskCreate(
        OtaFvuTaskFunction,
        "OTAFVU",
        4096,
        nullptr,
        2,
        &hTask
    ) != pdPASS) {
        ESP_LOGE(LOG_TAG, "Task creation failed!");
    }
#endif
    }
}