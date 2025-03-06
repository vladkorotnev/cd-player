#pragma once
#include <mode.h>
#include <esper-gui/views/framework.h>
#include <menus/framework.h>

class SettingsMode: public Mode {
    public:
        SettingsMode(const PlatformSharedResources res, ModeHost * host);
    
        void setup() override {
        }

        void loop() override {
            delay(100);
        }

        void on_remote_key_pressed(VirtualKey key) override {
            rootView.on_key_pressed(key);
        }

        void teardown() override {
        }
    
        UI::View& main_view() override {
            return rootView;
        }
    
    private:
        MenuNavigator rootView;
        const char * LOG_TAG = "SETTING";
};