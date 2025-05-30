#pragma once

#include "pipeline.h"
#include <menus/framework.h>
#include "source.h"

class FTPPlayer: public MenuPresentable {
public:
  FTPPlayer(FTPSource source, int startIndex, AudioStream * outPort):
  streamer(outPort), src(source), sIdx(startIndex),
    artist(std::make_shared<UI::Label>(EGRect{{0,0}, {160, 8}}, Fonts::FallbackWildcard8px, UI::Label::Alignment::Center)),
    title(std::make_shared<UI::Label>(EGRect{{0,8}, {160, 16}}, Fonts::FallbackWildcard16px, UI::Label::Alignment::Center)),
    UI::View() {
      artist->synchronize_scrolling_to(&title);
      artist->auto_scroll = true;
      title->synchronize_scrolling_to(&artist);
      title->auto_scroll = true;
      
      subviews.push_back(artist);
      subviews.push_back(title);

      streamer.metadataCallback = [this](MetaDataType type, std::string str) {
        if(str.length() == 0) return;

        if (type == MetaDataType::Artist) {
          artist->set_value(str);
        }
        else if (type == MetaDataType::Title) {
          title->set_value(str);
        }
      };
    }

  void on_presented() override {
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Info);
    auto s = src.nextStream(sIdx);
    if(s) {
      resetState();
      streamer.start(s);
    }
  }

  void on_dismissed() override {

  }

  void on_key_pressed(VirtualKey k, MenuNavigator *host) override {
    if(k == RVK_STOP || k == RVK_DEL) {
      host->pop();
    }
    else if(k == RVK_CURS_ENTER) {
      // PlayPause
    }
    else if(k == RVK_PAUSE) {

    }
    else if(k == RVK_PLAY) {

    }
    else if(k == RVK_TRACK_NEXT || k == RVK_CURS_DOWN) {

    }
    else if(k == RVK_TRACK_PREV || k == RVK_CURS_UP) {

    }
  }

protected:
  FTPStreamingPipeline streamer;
  FTPSource src;
  int sIdx;
  std::shared_ptr<UI::Label> artist;
  std::shared_ptr<UI::Label> title;

  void resetState() {
    artist->set_value("");
    title->set_value(src.currentFileName());
  }

  const char * LOG_TAG = "FTPPlayer";
};
