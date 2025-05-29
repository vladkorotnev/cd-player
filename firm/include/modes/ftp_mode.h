#pragma once
#include <mode.h>
#include <esper-gui/views/framework.h>
#include <menus/framework.h>
#include <platform.h>

class FTPNavigator: public MenuNavigator {
public:
    FTPNavigator(const PlatformSharedResources res, std::shared_ptr<MenuPresentable> root, EGRect f):
        resources(res),
        MenuNavigator(root, f) {}

    const PlatformSharedResources resources;
};

class FTPMode: public Mode {
    public:
        FTPMode(const PlatformSharedResources res, ModeHost * host);
    
        void setup() override {
          resources.router->activate_route(Platform::AudioRoute::ROUTE_INTERNAL_CPU);
        }

        void loop() override {
          delay(100);
          if(stopEject.is_clicked()) on_remote_key_pressed(VirtualKey::RVK_DEL);
          else if(playPause.is_clicked()) on_remote_key_pressed(VirtualKey::RVK_CURS_ENTER);
          else if(rewind.is_clicked()) on_remote_key_pressed(VirtualKey::RVK_CURS_LEFT);
          else if(forward.is_clicked()) on_remote_key_pressed(VirtualKey::RVK_CURS_RIGHT);
          else if(prevTrackDisc.is_clicked()) on_remote_key_pressed(VirtualKey::RVK_CURS_UP);
          else if(nextTrackDisc.is_clicked()) on_remote_key_pressed(VirtualKey::RVK_CURS_DOWN);
        }

        void on_remote_key_pressed(VirtualKey key) override {
          rootView.on_key_pressed(key);
        }

        void teardown() override {
          resources.router->activate_route(Platform::AudioRoute::ROUTE_NONE_INACTIVE);
        }
    
        UI::View& main_view() override {
            return rootView;
        }
    
    private:
        FTPNavigator rootView;
        Platform::Button stopEject;
        Platform::Button playPause;
        Platform::Button forward;
        Platform::Button rewind;
        Platform::Button nextTrackDisc;
        Platform::Button prevTrackDisc;
        Platform::Button playMode;
        const char * LOG_TAG = "FTPMode";
};