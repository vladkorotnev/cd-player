#pragma once
#include <mode.h>
#include <esper-core/service.h>
#include <localize.h>

class StandbyMode: public Mode {
    public:
        StandbyMode(const PlatformSharedResources res, ModeHost * host): 
            rootView({EGPointZero, res.display->get_size()}),
            lbl(std::make_shared<UI::Label>(EGRect {{0, res.display->get_size().height/2 - 8}, {res.display->get_size().width, 16}}, Fonts::FallbackWildcard16px, UI::Label::Alignment::Center)),
            Mode(res, host) {
                rootView.subviews.push_back(lbl);
        }
    
        void setup() override {
            lbl->set_value(localized_string("Goodbye!"));
            need_shutdown = true;
        }

        void loop() override {
            if (need_shutdown) {
                ESP_LOGW(LOG_TAG, "Entering standby");
                need_shutdown = false;
                if(resources.cdrom->check_media() == ATAPI::MediaTypeCode::MTC_DOOR_OPEN) {
                    resources.cdrom->eject(false);
                }
                resources.cdrom->start(false);
                resources.cdrom->set_tray_locked(true);
                resources.router->activate_route(Platform::AudioRoute::ROUTE_BYPASS);
                vTaskDelay(pdMS_TO_TICKS(250));
                resources.display->set_option(Graphics::Hardware::DriverOption::DISP_OPTION_POWER, false);
                esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
                Core::Services::WLAN::stop();
                lbl->set_value(localized_string("Hello!"));
                setCpuFrequencyMhz(80); // less than that breaks UART!!?
            }
            delay(100);
        }

        void teardown() override {
            setCpuFrequencyMhz(240);
            resources.cdrom->set_tray_locked(false);
            resources.display->set_option(Graphics::Hardware::DriverOption::DISP_OPTION_POWER, true);
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
        bool need_shutdown = false;
        const char * LOG_TAG = "STBY";
};