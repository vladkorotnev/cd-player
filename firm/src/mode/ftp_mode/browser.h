#pragma once
#include "modes/ftp_mode.h"
#include "icons.h"
#include "../settings/settings_icons.h"
#include <shared_prefs.h>
#include <FTPClient.h>
#include <menus/framework.h>
#include "player.h"

using ftp_client::FTPClient;

const Prefs::Key<std::string> PREFS_KEY_LAST_PATH = {"ftp_last", "/"};

class FTPBrowser: public MenuPresentable, public UI::ListView {
public:
  FTPNavigator * navigator = nullptr;
  const Fonts::Font * itemFont = Fonts::FallbackWildcard8px;
  FTPBrowser(Platform::AudioRouter * router):
    _spinner(std::make_shared<UI::TinySpinner>(EGRect {{160 - 5, 32 - 5}, {5, 5}})),
    client(),
    bgTask("FTPConnect", 8000),
    router_(router),
    UI::ListView(EGRectZero, {}) {
      subviews.push_back(_spinner);
    }

  void on_presented() override {
    current_dir = "/";
    _spinner->hidden = false;

    std::string ftpUrl = Prefs::get(PREFS_KEY_FTP_ADDRESS);
    if(ftpUrl == "") {
      _spinner->hidden = true;
      set_items({
        std::make_shared<UI::ListItem>(localized_string("(Connection Failed)"), [](EGGraphBuf *b, EGSize s) { }, nullptr, itemFont)
      });
      return;
    }

    int port = 21; //<- default FTP port
    size_t portSepPos = ftpUrl.find(":");
    size_t pathSepPos = ftpUrl.find("/");
    std::string ftpAddress = ftpUrl;

    if(portSepPos != std::string::npos) {
      port = std::stoi(ftpUrl.substr(portSepPos + 1, pathSepPos));
      ftpAddress = ftpUrl.substr(0, portSepPos);
    }

    if(pathSepPos != std::string::npos) {
      prefix = ftpUrl.substr(pathSepPos);
      if (portSepPos == std::string::npos) {
        ftpAddress = ftpUrl.substr(0, pathSepPos);
      }
    }

    set_items({
      std::make_shared<UI::ListItem>(ftpAddress, [](EGGraphBuf *b, EGSize s) { }, &icn_server, Fonts::FallbackWildcard16px),
    });

    bgTask.begin([this, ftpAddress, port]() {
      ESP_LOGI(LOG_TAG, "FTP CONNECT: [%s] PORT [%i] PREFIX [%s]", ftpAddress.c_str(), port, prefix.c_str());
      client.setPort(port);

      IPAddress resolved_addr(0,0,0,0);
      if (!WiFi.hostByName(ftpAddress.c_str(), resolved_addr)) {
        ESP_LOGE(LOG_TAG, "Failed to resolve host");
      } else {
        ESP_LOGI(LOG_TAG, "IP: [%i.%i.%i.%i]", resolved_addr[0], resolved_addr[1], resolved_addr[2], resolved_addr[3]);
      }

      if(client.begin(resolved_addr, Prefs::get(PREFS_KEY_FTP_USER).c_str(), Prefs::get(PREFS_KEY_FTP_PASS).c_str())) {
        client.binary();
        auto lastUsedDir = Prefs::get(PREFS_KEY_LAST_PATH);
        current_dir = lastUsedDir;
        list_current_dir();
      } else {
        _spinner->hidden = true;
        set_items({
          std::make_shared<UI::ListItem>(ftpAddress, [](EGGraphBuf *b, EGSize s) { }, nullptr, itemFont),
          std::make_shared<UI::ListItem>(localized_string("(Connection Failed)"), [](EGGraphBuf *b, EGSize s) { }, nullptr, itemFont),
        });
      }
      vTaskSuspend(NULL);
    });
  }

  void on_dismissed() override {

  }

  void on_key_pressed(VirtualKey k, MenuNavigator* host) override {
    if(k == RVK_CURS_DOWN) down();
    else if(k == RVK_CURS_UP) up();
    else if(k == RVK_CURS_ENTER) {
      int sel = selection;
      if (current_dir != "/") {
        sel = selection - 1;
      }
      if(sel < 0) {
        go_up();
        return;
      }
      if(sel >= current_list.size()) return;
      if(current_list[sel].isDir) {
        current_dir += current_list[sel].name + "/";
        backSelectionStack.push(selection);
        list_current_dir();
      } else {
        std::vector<std::string> names = {};
        auto port = router_->get_io_port_nub();
        int indexHere = -1;
        int indexThere = 0;
        int indexSelThere = 0;
        for(auto file: current_list) {
          indexHere++;
          if(file.isDir) continue;
          names.push_back(file.name);
          if (indexHere == sel) {
            indexSelThere = indexThere;
          }
          indexThere++;
        }
        FTPSource src(client, prefix + current_dir, names);
        ESP_LOGI(LOG_TAG, "Playlist size=[%i] pos=[%i], in=[%s], outPort=[%p]", names.size(), indexSelThere, current_dir.c_str(), port);
        host->push(std::make_shared<FTPPlayer>(src, indexSelThere, port));
      }
    }
    else if(k == RVK_CURS_LEFT) {
      go_up();
    }
  }
protected:
  std::shared_ptr<UI::TinySpinner> _spinner;
  FTPClient<WiFiClient> client;
  std::string prefix = "";
  std::string current_dir = "/";
  Platform::AudioRouter * router_ = nullptr;
  struct FTPEntry {
    bool isDir;
    std::string name;
  };
  std::vector<FTPEntry> current_list = {};
  const std::vector<std::string> supported_extensions = {
    ".mp3", ".m4a", ".wav", ".flac"
  };

  void go_up() {
    if(current_dir != "/") {
      if(current_dir.at(current_dir.length() - 1) == '/') {
        current_dir = current_dir.substr(0, current_dir.length() - 1);
      }

      size_t last_slash = current_dir.rfind('/');
      if(last_slash != std::string::npos) {
        current_dir = current_dir.substr(0, last_slash);
        current_dir += "/";
      }
      list_current_dir();
      if(!backSelectionStack.empty()) {
        int backSel = backSelectionStack.top();
        backSelectionStack.pop();
        if(backSel < _items.size() && backSel > 0) {
          select(backSel);
        }
      }
    }
  }

  void list_current_dir() {
    std::string actual_dir = prefix + current_dir;
    ESP_LOGI(LOG_TAG, "List: [%s]", actual_dir.c_str());
    _spinner->hidden = false;
    current_list.clear();
    _items.clear();
    std::vector<std::shared_ptr<UI::ListItem>> viewItems = {};
    for (auto file : client.ls(actual_dir.c_str()))  {
      const std::string name = std::string(file.name()).substr(actual_dir.length());
      if (name.at(0) == '.') continue;
      bool isDir = file.isDirectory();
      if (!isDir) {
        bool supported = false;
        size_t ext_pos = name.find_last_of(".");
        if (ext_pos == std::string::npos) continue;
        std::string extension = name.substr(ext_pos);
        std::transform(extension.begin(), extension.end(), extension.begin(), [](char c){ return std::tolower(c); });
        for (auto ext : supported_extensions) {
          if (extension == ext) {
            supported = true;
            break;
          }
        }
        if (!supported) continue;
      }
      current_list.push_back({isDir, name});
      viewItems.push_back(std::make_shared<UI::ListItem>(name, [](EGGraphBuf *b, EGSize s) {}, isDir ? &icn_dir : &icn_file, itemFont));
    }
    if (viewItems.empty()) {
      viewItems.push_back(std::make_shared<UI::ListItem>(localized_string("(Empty)"), [](EGGraphBuf *b, EGSize s) { }, nullptr, itemFont));
    }
    if (current_dir != "/") {
      std::string up_name = "..";
      size_t parent_name_pos = current_dir.rfind('/', current_dir.length() - 2);
      if (parent_name_pos != std::string::npos) {
        up_name = current_dir.substr(parent_name_pos + 1, current_dir.length() - 1);
      }
      viewItems.insert(viewItems.begin(), std::make_shared<UI::ListItem>(up_name, [](EGGraphBuf *b, EGSize s) { }, &icn_dir_open, itemFont));
    }
    set_items(viewItems);
    if (current_dir != "/") {
      for(auto& item: _items) {
        if (item == _items[0]) continue;
        item->frame.origin.x += 4;
        item->frame.size.width -= 4;
      }
    }
    Prefs::set(PREFS_KEY_LAST_PATH, current_dir);
    _spinner->hidden = true;
  }

private:
  const char * LOG_TAG = "FTPBro";
  std::stack<int> backSelectionStack = {};
  Task bgTask;
};
