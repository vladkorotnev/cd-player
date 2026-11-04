#pragma once
#include "platform.h"
#include <esper-gui/views/toast.h>
#include <esper-cdp/player.h>
#include <esper-core/spdif.h>

class ModeHost;

class Mode {
friend class ModeHost;
public:
    Mode(const PlatformSharedResources res, ModeHost * host):
        resources(res),
        _host(host),
        _toast(std::make_shared<UI::Toast>([this] {
            main_view().set_needs_display();
        })) {}
    virtual ~Mode() = default;
    virtual void setup() {};
    virtual UI::View& main_view() = 0;
    virtual void loop() {};
    virtual void on_remote_key_pressed(VirtualKey key) {};
    virtual void teardown() {};
protected:
    const PlatformSharedResources resources;

    void show_toast(const std::string& text, const TickType_t duration = pdMS_TO_TICKS(1000)) {
        auto& view = main_view();
        _toast->show(
            text,
            Fonts::FallbackWildcard8px,
            duration,
            view.frame.size
        );

        auto& subviews = view.subviews;
        subviews.erase(std::remove(subviews.begin(), subviews.end(), _toast), subviews.end());
        subviews.push_back(_toast);
        view.set_needs_display();
    }
private:
    ModeHost * const _host;
    std::shared_ptr<UI::Toast> _toast;
};
