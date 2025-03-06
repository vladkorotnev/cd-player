#include <modes/settings_mode.h>
#include <mode_host.h>
#include <shared_prefs.h>
#include <consts.h>
#include <dirent.h>

// By PiiXL
// https://piiixl.itch.io/mega-1-bit-icons-bundle
const uint8_t icn_wifi_data[] = {
    0x3e, 0x0c, 0x61, 0x1e, 0x5f, 0xde, 0x40, 0x0c, 0x40, 0x00, 0x41, 0x48, 0x22, 0xa8, 0x51, 0x74, 
    0x6a, 0xf6, 0x37, 0xf6, 0x1b, 0xf6, 0x4d, 0xf6, 0x66, 0xfc, 0x70, 0x00, 0x58, 0x00, 0xff, 0xc0
};
const uint8_t icn_cd_data[] = {
    0xff, 0xff, 0xff, 0xff, 0xd7, 0xff, 0xff, 0xff, 0xd7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0x00, 0x00, 0xff, 0xff, 0x80, 0x01, 0xa2, 0x45, 0xa1, 0x85, 0x10, 0x08, 0x08, 0x10, 0x07, 0xe0,         
};
const uint8_t icn_radio_data[] = {
    0x00, 0x1c, 0x00, 0xe0, 0x07, 0x00, 0x00, 0x00, 0xdf, 0xff, 0xd0, 0x01, 0xd5, 0x55, 0xd1, 0x01, 
    0xdf, 0xff, 0xda, 0xff, 0xd5, 0x49, 0xda, 0xc9, 0xd5, 0x7f, 0xda, 0xff, 0xdf, 0xff, 0xdf, 0xff, 
};
const uint8_t icn_bt_data[] = {
    0x00, 0x00, 0x0b, 0xf8, 0x14, 0x04, 0x15, 0x54, 0x14, 0x04, 0x15, 0x54, 0x14, 0x04, 0x15, 0x14, 
    0x14, 0x34, 0x14, 0x74, 0x14, 0xf4, 0x15, 0xf4, 0x14, 0x04, 0x17, 0xfc, 0x17, 0xbc, 0x0b, 0xf8, 
};
const uint8_t icn_sys_data[] = {
    0x00, 0x00, 0x03, 0xe0, 0x07, 0xc0, 0x0f, 0x80, 0x0f, 0x00, 0x0f, 0x00, 0x0f, 0x01, 0x0f, 0x83, 
    0x0f, 0xc7, 0x1f, 0xff, 0x3f, 0xff, 0x6f, 0xfe, 0x5f, 0xfc, 0x3b, 0x00, 0x76, 0x00, 0x6c, 0x00
};
const uint8_t icn_about_data[] = {
    0x00, 0x00, 0x0f, 0xf0, 0x11, 0xf8, 0x3e, 0xfc, 0x7b, 0xc2, 0x7b, 0xfe, 0x7b, 0xce, 0x7f, 0xce, 
    0x7f, 0xfe, 0x6e, 0x3e, 0x57, 0xfe, 0x50, 0x3e, 0x3f, 0xdc, 0x7c, 0x38, 0x7d, 0xf0, 0x3c, 0x00
};

const uint8_t icn_spkr_data[] = {
    0x00, 0x00, 0xab, 0xff, 0x53, 0xcf, 0xab, 0xcf, 0x53, 0xff, 0xaa, 0x85, 0x53, 0x03, 0xaa, 0x01, 
	0x52, 0x01, 0xaa, 0x11, 0x52, 0x85, 0xaa, 0xcd, 0x52, 0xfd, 0xab, 0x7b, 0x52, 0x85, 0xab, 0xff
};

const EGImage icn_wifi = {
    .format = EG_FMT_HORIZONTAL,
    .size = {16, 16},
    .data = icn_wifi_data
};
const EGImage icn_cd = {
    .format = EG_FMT_HORIZONTAL,
    .size = {16, 16},
    .data = icn_cd_data
};
const EGImage icn_radio = {
    .format = EG_FMT_HORIZONTAL,
    .size = {16, 16},
    .data = icn_radio_data
};
const EGImage icn_bt = {
    .format = EG_FMT_HORIZONTAL,
    .size = {16, 16},
    .data = icn_bt_data
};
const EGImage icn_sys = {
    .format = EG_FMT_HORIZONTAL,
    .size = {16, 16},
    .data = icn_sys_data
};
const EGImage icn_about = {
    .format = EG_FMT_HORIZONTAL,
    .size = {16, 16},
    .data = icn_about_data
};
const EGImage icn_spkr = {
    .format = EG_FMT_HORIZONTAL,
    .size = {16, 16},
    .data = icn_spkr_data
};

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

SettingsMode::SettingsMode(const PlatformSharedResources res, ModeHost * host): 
    rootView(std::make_shared<ListMenuNode>(
        ListMenuNode(
            "Menu",
            nullptr,
            std::tuple {
                ListMenuNode("Source", &icn_spkr, std::tuple {
                    ActionMenuNode("CD", [host](MenuNavigator *) { host->activate_mode(ESPER_MODE_CD); }, &icn_cd),
                    ActionMenuNode("Web Radio", [host](MenuNavigator *) { host->activate_mode(ESPER_MODE_NET_RADIO); }, &icn_radio),
                    ActionMenuNode("Bluetooth", [host](MenuNavigator *) { host->activate_mode(ESPER_MODE_BLUETOOTH); }, &icn_bt),
                }),
                ListMenuNode("Settings", &icn_sys, std::tuple {
                    MenuNode("WiFi", &icn_wifi),
                    ListMenuNode("CD", &icn_cd, std::tuple {
                        TogglePreferenceMenuNode("Show Lyrics", PREFS_KEY_CD_LYRICS_ENABLED),
                        ListMenuNode("Metadata", nullptr, std::tuple {
                            TogglePreferenceMenuNode("Cache Metadata", PREFS_KEY_CD_CACHE_META),
                            ActionMenuNode("Clear Cache", [](MenuNavigator* h) {
                                bool res = clear_cddb_cache();
                                h->push(std::make_shared<InfoMessageBox>(res ? "Cache cleared" : "Failed to clear cache"));
                            }),
                            ListMenuNode("Metadata sources", nullptr, std::tuple {
                                TogglePreferenceMenuNode("MusicBrainz", PREFS_KEY_CD_MUSICBRAINZ_ENABLED),
                                TogglePreferenceMenuNode("CDDB", PREFS_KEY_CD_CDDB_ENABLED),
                                TextPreferenceEditorNode("CDDB server", PREFS_KEY_CDDB_ADDRESS),
                                TextPreferenceEditorNode("CDDB e-mail", PREFS_KEY_CDDB_EMAIL)
                            }),
                            ListMenuNode("Lyrics sources", nullptr, std::tuple {
                                TogglePreferenceMenuNode("LRCLib", PREFS_KEY_CD_LRCLIB_ENABLED)
                            })
                        }),
                        ListMenuNode("Last.FM", nullptr, std::tuple {
                            TogglePreferenceMenuNode("Enable", PREFS_KEY_CD_LASTFM_ENABLED),
                            MenuNode("Login")
                        })
                    }),
                    MenuNode("Radio Stations", &icn_radio),
                    ListMenuNode("Bluetooth", &icn_bt, std::tuple {
                        MenuNode("Device Name"),
                        TogglePreferenceMenuNode("Require PIN code", PREFS_KEY_BT_NEED_PIN),
                        TogglePreferenceMenuNode("Auto-connect", PREFS_KEY_BT_RECONNECT),
                    }),
                    ListMenuNode("System", &icn_sys, std::tuple {
                        ListMenuNode("Language", nullptr, std::tuple {
                            MenuNode("English"),
                            MenuNode("Русский"),
                            MenuNode("日本語")
                        }),
                        ListMenuNode("Mode toggle button", nullptr, std::tuple {
                            TogglePreferenceMenuNode("CD", PREFS_KEY_CD_MODE_INCLUDED),
                            TogglePreferenceMenuNode("Web Radio", PREFS_KEY_RADIO_MODE_INCLUDED),
                            TogglePreferenceMenuNode("Bluetooth", PREFS_KEY_BLUETOOTH_MODE_INCLUDED),
                        }),
                        MenuNode("Check for Updates"),
                    }),
    
                    ListMenuNode("About", nullptr, std::tuple {
                        MenuNode("TO-DO"),
                    }),
                }),
            }
        )
    ), {EGPointZero, {160, 32}}),
    Mode(res, host) {
}