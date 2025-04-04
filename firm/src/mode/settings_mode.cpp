#include <modes/settings_mode.h>
#include <mode_host.h>
#include <shared_prefs.h>
#include <consts.h>
#include <dirent.h>
#include <LittleFS.h>
#include <localize.h>
#include "settings/settings_icons.h"
#include "settings/wifi_menus.h"
#include "settings/radio_menus.h"
#include "settings/httpfvu_app.h"
#include "settings/cd_diag_app.h"
#include <esper-core/wlan.h>
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include <esp_system.h>

static std::string format_disk_space(size_t disk_space) {
    std::string space_str;

    if (disk_space > 1024 * 1024) {
        space_str = std::to_string(disk_space / 1024 / 1024) + "M";
    }
    else if (disk_space >= 1024) {
        space_str = std::to_string(disk_space / 1024) + "K";
    }
    else {
        space_str = std::to_string(disk_space) + "B";
    }
    return space_str;
}

static bool clear_cddb_cache() {
    const char LOG_TAG[] = "ClrCache";
    const char* dir_path = META_CACHE_PREFIX;
    bool rslt = true;
    DIR* dir = opendir(dir_path);
    if (dir == NULL) {
        ESP_LOGE(LOG_TAG, "Failed to open directory: %s", dir_path);
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue; // Skip "." and ".." entries
        }

        // Construct the full file path
        char file_path[256]; // Assuming max file path length of 256
        snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, entry->d_name);

        // Delete the file
        if (remove(file_path) == 0) {
            ESP_LOGI(LOG_TAG, "Deleted: %s", file_path);
        } else {
            ESP_LOGE(LOG_TAG, "Failed to delete: %s", file_path);
            rslt = false;
        }
    }

    closedir(dir);
    ESP_LOGI(LOG_TAG, "CDDB cache cleared.");
    return rslt;
}

class LanguageMenuNode: public MenuNode {
public:
    LanguageMenuNode(DisplayLanguage lang): 
        language(lang),
        MenuNode(language_name(lang)) {}

    void execute(MenuNavigator * host) const override {
        set_active_language(language);
        host->push(std::make_shared<InfoMessageBox>(localized_string("Language changed"), [](MenuNavigator* h) { ESP.restart(); }));
    }

    void draw_accessory(EGGraphBuf* buf, EGSize bounds) const override {
        if(active_language() == language) {
            EGBlitImage(buf, {bounds.width - icn_checkmark.size.width, bounds.height/2 - icn_checkmark.size.height/2}, &icn_checkmark);
        }
        MenuNode::draw_accessory(buf, bounds);
    }

protected:
    DisplayLanguage language;
};

static const ListMenuNode settings_menu("Settings", &icn_sys, std::tuple {
    WiFiNetworksListMenuNode(),
    ListMenuNode("CD", &icn_cd, std::tuple {
        TogglePreferenceMenuNode("Show Lyrics", PREFS_KEY_CD_LYRICS_ENABLED),
        ListMenuNode("Metadata", nullptr, std::tuple {
            TogglePreferenceMenuNode("Cache Metadata", PREFS_KEY_CD_CACHE_META),
            ActionMenuNode("Clear Cache", [](MenuNavigator* h) {
                bool res = clear_cddb_cache();
                h->push(std::make_shared<InfoMessageBox>(res ? localized_string("Cache cleared") : localized_string("Failed to clear cache")));
            }),
            ListMenuNode("Metadata sources", nullptr, std::tuple {
                TogglePreferenceMenuNode("MusicBrainz", PREFS_KEY_CD_MUSICBRAINZ_ENABLED),
                TogglePreferenceMenuNode("CDDB", PREFS_KEY_CD_CDDB_ENABLED),
                TextPreferenceEditorNode("CDDB server", PREFS_KEY_CDDB_ADDRESS),
                TextPreferenceEditorNode("CDDB e-mail", PREFS_KEY_CDDB_EMAIL)
            }),
            ListMenuNode("Lyrics sources", nullptr, std::tuple {
                TogglePreferenceMenuNode("LRCLib", PREFS_KEY_CD_LRCLIB_ENABLED),
                TogglePreferenceMenuNode("NetEase (163)", PREFS_KEY_CD_NETEASE_ENABLED),
                TogglePreferenceMenuNode("QQ Music", PREFS_KEY_CD_QQ_ENABLED),
            })
        }),
        ListMenuNode("Last.FM", nullptr, std::tuple {
            TextPreferenceEditorNode("User name", PREFS_KEY_CD_LASTFM_USER),
            TextPreferenceEditorNode("Password", PREFS_KEY_CD_LASTFM_PASS),
            TogglePreferenceMenuNode("Enable", PREFS_KEY_CD_LASTFM_ENABLED),
        })
    }),
    RadioStationEditorNode(),
    ListMenuNode("Bluetooth", &icn_bt, std::tuple {
        TextPreferenceEditorNode("Device Name", PREFS_KEY_BT_NAME),
        TogglePreferenceMenuNode("Require PIN code", PREFS_KEY_BT_NEED_PIN),
        TogglePreferenceMenuNode("Auto-connect", PREFS_KEY_BT_RECONNECT),
    }),
    ListMenuNode("System", &icn_sys, std::tuple {
        ListMenuNode("Language", nullptr, std::tuple {
            LanguageMenuNode(DSPL_LANG_EN),
            LanguageMenuNode(DSPL_LANG_RU),
            LanguageMenuNode(DSPL_LANG_JA),
            LanguageMenuNode(DSPL_LANG_HU),
            LanguageMenuNode(DSPL_LANG_DE),
            LanguageMenuNode(DSPL_LANG_NL),
        }),
        ListMenuNode("Mode toggle button", nullptr, std::tuple {
            TogglePreferenceMenuNode("CD", PREFS_KEY_CD_MODE_INCLUDED),
            TogglePreferenceMenuNode("Web Radio", PREFS_KEY_RADIO_MODE_INCLUDED),
            TogglePreferenceMenuNode("Bluetooth", PREFS_KEY_BLUETOOTH_MODE_INCLUDED),
        }),
#if OTA_FVU_ENABLED
        TogglePreferenceMenuNode("OTA FVU", PREFS_KEY_OTAFVU_ALLOWED),
#endif
        ActionMenuNode("Check for Updates", [](MenuNavigator* h) { 
            h->push(std::make_shared<HttpFvuApp>(h));
        }),
        TextPreferenceEditorNode("NTP server", Core::PrefsKey::NTP_SERVER),
        ListMenuNode("Full Reset", nullptr, std::tuple {
            ListMenuNode("Yes, Erase All!", nullptr, std::tuple {
                ActionMenuNode("Yes, I Am Sure!", [](MenuNavigator* h) {
                    clear_cddb_cache();
                    Prefs::reset_all();
                    h->push(std::make_shared<InfoMessageBox>(localized_string("Reset complete"), [](MenuNavigator*) { ESP.restart(); }));
                }),
            })
        }),
    }),
    ListMenuNode("About", &icn_about, std::tuple {
        DetailTextMenuNode("", "ESPer-CDP"),
        DetailTextMenuNode("Ver.", esp_ota_get_app_description()->version),
        DetailTextMenuNode("Type", FVU_FLAVOR),
        DetailTextMenuNode("Memory", []() { return format_disk_space(LittleFS.totalBytes()); }),
        DetailTextMenuNode("Used", []() { return format_disk_space(LittleFS.usedBytes()); }),
        DetailTextMenuNode("Free", []() { return format_disk_space(LittleFS.totalBytes() - LittleFS.usedBytes()); }),
        DetailTextMenuNode("Network", []() { return Core::Services::WLAN::network_name(); }),
        DetailTextMenuNode("IP", []() { return Core::Services::WLAN::current_ip(); }),
        ActionMenuNode("CD Diagnostics", [](MenuNavigator* h) { 
            h->push(std::make_shared<CDDiagsMenuNode>((SettingsMenuNavigator *) h));
        }),
    }),
});

SettingsMode::SettingsMode(const PlatformSharedResources res, ModeHost * host):
    stopEject (resources.keypad, (1 << 0)),
    playPause (resources.keypad, (1 << 1)),
    rewind (resources.keypad, (1 << 2)),
    forward (resources.keypad, (1 << 3)),
    prevTrackDisc (resources.keypad, (1 << 4)),
    nextTrackDisc (resources.keypad, (1 << 5)),
    playMode (resources.keypad, (1 << 6)),
    rootView(resources, std::make_shared<ListMenuNode>(
        ListMenuNode(
            "Menu",
            nullptr,
            std::tuple {
                ListMenuNode("Source", &icn_spkr, std::tuple {
                    ActionMenuNode("CD", [host](MenuNavigator *) { host->activate_mode(ESPER_MODE_CD); }, &icn_cd),
                    ActionMenuNode("Web Radio", [host](MenuNavigator *) { host->activate_mode(ESPER_MODE_NET_RADIO); }, &icn_radio),
                    ActionMenuNode("Bluetooth", [host](MenuNavigator *) { host->activate_mode(ESPER_MODE_BLUETOOTH); }, &icn_bt),
                }),
                settings_menu
            }
        )   
    ), {EGPointZero, {160, 32}}),
    Mode(res, host) {
}
