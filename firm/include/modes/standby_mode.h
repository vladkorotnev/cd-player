#pragma once
#include <mode.h>
#include <esper-core/service.h>
#include <localize.h>

class StandbyMode: public Mode {
    public:
        StandbyMode(const PlatformSharedResources res, ModeHost * host): 
            rootView({EGPointZero, {160, 32}}),
            lbl(std::make_shared<UI::Label>(EGRect {{0, 8}, {160, 16}}, Fonts::FallbackWildcard16px, UI::Label::Alignment::Center)),
            Mode(res, host) {
                rootView.subviews.push_back(lbl);
        }
    
        void setup() override {
            lbl->set_value(localized_string("Goodbye!"));
            ESP_LOGW(LOG_TAG, "Entering standby");
            if(resources.cdrom->check_media() == ATAPI::MediaTypeCode::MTC_DOOR_OPEN) {
                resources.cdrom->eject(false);
            }
            resources.cdrom->start(false);
            resources.cdrom->set_tray_locked(true);
            resources.router->activate_route(Platform::AudioRoute::ROUTE_BYPASS);
            vTaskDelay(pdMS_TO_TICKS(250));
            resources.display->set_power(false);
            esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
            Core::Services::WLAN::stop();
            setCpuFrequencyMhz(80); // less than that breaks UART!!?
        }

        void loop() override {
            delay(100);
        }

        void teardown() override {
            setCpuFrequencyMhz(240);
            lbl->set_value(localized_string("Hello!"));
            resources.cdrom->set_tray_locked(false);
            resources.display->set_power(true);
            resources.router->activate_route(Platform::AudioRoute::ROUTE_NONE_INACTIVE);
            esp_wifi_set_ps(WIFI_PS_NONE);
            Core::Services::WLAN::start();
            ESP_LOGW(LOG_TAG, "leaving standby");
        }
    
        UI::View& main_view() override {
            return rootView;
        }
    
    private:
        UI::View rootView;
        std::shared_ptr<UI::Label> lbl;
        const char * LOG_TAG = "STBY";
};