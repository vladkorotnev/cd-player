#pragma once
#include <esper-gui/views/framework.h>
#include <modes/netradio_mode.h>
#include <views/wifi_icon.h>
#include <utils.h>
#include "channel_grid.h"

using std::make_shared;
using std::shared_ptr;

class InternetRadioDiagsView: public UI::View {
public:
  InternetRadioDiagsView(EGRect f): View(f) {
    bufferHealth = make_shared<UI::ProgressBar>(EGRect{{32, 0},{112, 6}});
    subviews.push_back(bufferHealth);
    enoughMark = make_shared<UI::ProgressBar>(EGRect{{32, 6},{112, 2}});
    enoughMark->border = false;
    enoughMark->filled = false;
    subviews.push_back(enoughMark);

    bufferSize = make_shared<UI::Label>(EGRect{EGPointZero, {32, 8}}, Fonts::FallbackWildcard8px, UI::Label::Alignment::Right);
    subviews.push_back(bufferSize);
    codecName = make_shared<UI::Label>(EGRect{{0,8}, {24, 8}}, Fonts::FallbackWildcard8px, UI::Label::Alignment::Left);
    subviews.push_back(codecName);
    bitrate = make_shared<UI::Label>(EGRect{{24, 8}, {56, 8}}, Fonts::FallbackWildcard8px, UI::Label::Alignment::Right);
    subviews.push_back(bitrate);
    speed = make_shared<UI::Label>(EGRect{{104, 8}, {56, 8}}, Fonts::FallbackWildcard8px, UI::Label::Alignment::Right);
    subviews.push_back(speed);
    avgSpeedLbl = make_shared<UI::Label>(EGRect{{104, 16}, {56, 8}}, Fonts::FallbackWildcard8px, UI::Label::Alignment::Right);
    subviews.push_back(avgSpeedLbl);
  }

  void set_data(int bufPct, int bufBytes, const std::string& codec, int enoughPct, int bytesPerSecond, int bps) {
    bufferHealth->value = bufPct;
    bufferSize->set_value(format_bytes(bufBytes));
    codecName->set_value(codec);
    enoughMark->value = enoughPct;
    enoughMark->blinking = bufPct >= enoughPct;
    speed->set_value(format_bytes_as_bits(bytesPerSecond)+"/s");
    bitrate->set_value(format_bits(bps)+"ps");
    avgSpeedValue += bytesPerSecond;
    avgSpeedValue /= 2;
    avgSpeedLbl->set_value(format_bytes_as_bits(avgSpeedValue)+"/s");
  }

private:
  int avgSpeedValue = 0;
  shared_ptr<UI::ProgressBar> bufferHealth;
  shared_ptr<UI::ProgressBar> enoughMark;
  shared_ptr<UI::Label> bufferSize;
  shared_ptr<UI::Label> codecName;
  shared_ptr<UI::Label> speed;
  shared_ptr<UI::Label> avgSpeedLbl;
  shared_ptr<UI::Label> bitrate;
};

class InternetRadioMode::InternetRadioView: public UI::View {
public:
    shared_ptr<WiFiIcon> wifi;
    shared_ptr<UI::TinySpinner> loading;
    shared_ptr<UI::Label> lblWarning;

    shared_ptr<UI::Label> lblTitle;
    shared_ptr<UI::Label> lblSubtitle;

    shared_ptr<ChannelGridBar> channelBar;

    shared_ptr<UI::View> normalGroup;
    shared_ptr<InternetRadioDiagsView> diagsGroup;

    InternetRadioView(EGRect f): View(f) {
        normalGroup = make_shared<UI::View>(EGRect {EGPointZero, {160, 24}});
        {
          lblTitle = make_shared<UI::Label>(EGRect {{0, 8}, {160, 16}}, Fonts::FallbackWildcard16px, UI::Label::Alignment::Center);
          lblSubtitle = make_shared<UI::Label>(EGRect {EGPointZero, {160, 8}}, Fonts::FallbackWildcard8px, UI::Label::Alignment::Center);

          lblTitle->auto_scroll = true;
          lblTitle->synchronize_scrolling_to(&lblSubtitle);
          lblSubtitle->auto_scroll = true;
          lblSubtitle->synchronize_scrolling_to(&lblTitle);

          normalGroup->subviews.push_back(lblTitle);
          normalGroup->subviews.push_back(lblSubtitle);
        }
        subviews.push_back(normalGroup);

        diagsGroup = make_shared<InternetRadioDiagsView>(normalGroup->frame);
        diagsGroup->hidden = true;
        subviews.push_back(diagsGroup);

        wifi = make_shared<WiFiIcon>(EGRect {{0, 32 - 7}, {5, 5}});
        subviews.push_back(wifi);

        loading = make_shared<UI::TinySpinner>(EGRect {{160 - 6, 32 - 7}, {5, 5}});
        loading->hidden = true;
        subviews.push_back(loading);

        channelBar = make_shared<ChannelGridBar>(EGRect {{5, 32 - 7}, {160 - 12, 7}});
        subviews.push_back(channelBar);

        lblWarning = make_shared<UI::Label>(EGRect {{160 - 6, 32 - 7}, {5, 5}}, Fonts::TinyDigitFont, UI::Label::Alignment::Center, "!");
        lblWarning->hidden = true;
        subviews.push_back(lblWarning);
    }

    void update_meta(MetaDataType type, const char * str, int len) {
        if(type == MetaDataType::Title) {
            if(len > 0) {
                has_metadata = true;
                lblSubtitle->set_value(station);
                lblTitle->set_value(str);
            } else if(has_metadata) {
                has_metadata = false;
                lblSubtitle->set_value("");
                lblTitle->set_value(station);
            }
        }
        else if(type == MetaDataType::Name) {
            // usually just the station ID
            station = str;
            if(has_metadata) {
                lblSubtitle->set_value(station);
            } 
            else {
                lblTitle->set_value(station);
            }
        }
    }

    void reset_meta(const std::string station_name) {
        lblSubtitle->set_value("");
        lblTitle->set_value(station_name);
        station = station_name;
        has_metadata = false;
    }

    void set_loading(bool isLoading) {
        loading->hidden = !isLoading;
    }

    void toggle_diags() {
        diagsGroup->hidden = !diagsGroup->hidden;
        normalGroup->hidden = !normalGroup->hidden;
        ESP_LOGI("IRAView", "Diags is now %s", diagsGroup->hidden ? "off" : "ON");
    }
private:
    bool has_metadata = false; // if true, station title goes on top and track name is the big one
    std::string station = "";
};
