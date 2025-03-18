#pragma once
#include <httpfvu.h>
#include <menus/framework.h>
#include <esper-core/prefs.h>

Prefs::Key<bool> PREFS_KEY_NEED_FVU_POPUP { "fvuflag", false };

class HttpFvuApp: public MenuPresentable {
public:
    HttpFvuApp(MenuNavigator * host): 
        _host(host),
        nav_locked(true),
        checkSpinner(std::make_shared<UI::BigSpinner>(EGRectZero)),
        checkLabel(std::make_shared<UI::Label>(EGRectZero, Fonts::FallbackWildcard16px, UI::Label::Alignment::Center, localized_string("Checking..."))),
        updTitleLabel(std::make_shared<UI::Label>(EGRectZero, Fonts::FallbackWildcard8px, UI::Label::Alignment::Center, localized_string("Update in progress"))),
        updSubtitleLabel(std::make_shared<UI::Label>(EGRectZero, Fonts::FallbackWildcard8px, UI::Label::Alignment::Center, localized_string("DO NOT TURN OFF!!"))),
        updateProgress(std::make_shared<UI::ProgressBar>(EGRectZero)),
        MenuPresentable() {
            subviews = {
                checkSpinner,
                checkLabel,
                updTitleLabel,
                updSubtitleLabel,
                updateProgress
            };
    }

    bool navigation_locked() override {
        return nav_locked;
    }

    bool needs_display() override {
        if(subtitleBlinks) {
            const TickType_t now = xTaskGetTickCount();
            if(now - subtitleBlinkTime >= pdMS_TO_TICKS((updSubtitleLabel->hidden ? 250 : 650))) {
                updSubtitleLabel->hidden = !updSubtitleLabel->hidden;
                subtitleBlinkTime = now;
            }
        }
        return MenuPresentable::needs_display();
    }

    void on_presented() override {
        nav_locked = true;
        show_checking();
        bool need_fvu = HTTPFVU::is_new_version_available();
        if(!need_fvu) {
            nav_locked = false;
            _host->push(std::make_shared<InfoMessageBox>(localized_string("No new version"), [](MenuNavigator *h){ h->pop_to_root(); }));
        } else {
            show_installing();
            HTTPFVU::update_file_system([this](HTTPFVU::HttpFvuStatus sts, int pct) {
                if(sts == HTTPFVU::HttpFvuStatus::HTTP_FVU_SUCCESS) {
                    HTTPFVU::update_firmware([this](HTTPFVU::HttpFvuStatus sts, int pct) {
                        if(sts == HTTPFVU::HttpFvuStatus::HTTP_FVU_SUCCESS) {
                            updTitleLabel->set_value(localized_string("Update completed"));
                            subtitleBlinks = false;
                            updSubtitleLabel->hidden = true;
                            updateProgress->value = 100;
                            delay(2000);
                            ESP.restart();
                        }
                        else if(sts == HTTPFVU::HttpFvuStatus::HTTP_FVU_ERROR) {
                            updTitleLabel->set_value(localized_string("Update error!"));
                            updSubtitleLabel->set_value(localized_string("ROM update"));
                            subtitleBlinks = false;
                            updSubtitleLabel->hidden = false;
                            updateProgress->hidden = true;
                        }
                        else if(sts == HTTPFVU::HttpFvuStatus::HTTP_FVU_IN_PROGRESS) {
                            updateProgress->value = pct;
                        }
                    });
                }
                else if(sts == HTTPFVU::HttpFvuStatus::HTTP_FVU_ERROR) {
                    updTitleLabel->set_value(localized_string("Update error!"));
                    updSubtitleLabel->set_value(localized_string("FS update"));
                    subtitleBlinks = false;
                    updSubtitleLabel->hidden = false;
                    updateProgress->hidden = true;
                }
                else if(sts == HTTPFVU::HttpFvuStatus::HTTP_FVU_IN_PROGRESS) {
                    updateProgress->value = pct;
                }
            });
        }
    }

protected:
    bool nav_locked;
    MenuNavigator * _host;
    std::shared_ptr<UI::BigSpinner> checkSpinner;
    std::shared_ptr<UI::Label> checkLabel;
    std::shared_ptr<UI::Label> updTitleLabel;
    std::shared_ptr<UI::Label> updSubtitleLabel;
    TickType_t subtitleBlinkTime;
    bool subtitleBlinks;
    std::shared_ptr<UI::ProgressBar> updateProgress;

    void show_checking() {
        updTitleLabel->hidden = true;
        updSubtitleLabel->hidden = true;
        updateProgress->hidden = true;

        checkLabel->hidden = false;
        checkSpinner->hidden = false;

        checkSpinner->frame = EGRect { {8, 8}, {16, 16} };
        checkSpinner->set_layout();
        checkLabel->frame = EGRect { {checkSpinner->frame.origin.x + checkSpinner->frame.size.width + 6, frame.size.height/2-8}, {frame.size.width - (checkSpinner->frame.origin.x + checkSpinner->frame.size.width + 6), 16} };
        
        set_needs_display();
    }

    void show_installing() {
        checkLabel->hidden = true;
        checkSpinner->hidden = true;

        updTitleLabel->hidden = false;
        updSubtitleLabel->hidden = false;
        updateProgress->hidden = false;

        updateProgress->frame = EGRect {{8, 0}, {frame.size.width - 16, 8}};
        updTitleLabel->frame = EGRect {{0, 12}, {frame.size.width, 10}};
        updSubtitleLabel->frame = EGRect {{0, 24}, {frame.size.width, 8}};

        subtitleBlinkTime = xTaskGetTickCount();
        subtitleBlinks = true;
        set_needs_display();
    }
};