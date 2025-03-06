#include <localize.h>
#include <esp_attr.h>
#include <map>
#include <esper-core/prefs.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#ifndef LANG_DIR_PREFIX
#define LANG_DIR_PREFIX "/lang"
#endif

static const char LOG_TAG[] = "LOCA";

const Prefs::Key<int> ACTIVE_LANGUAGE_PREFS_KEY = {"lang", DSPL_LANG_EN};

DisplayLanguage active_language() {
    return (DisplayLanguage) Prefs::get(ACTIVE_LANGUAGE_PREFS_KEY);
}

void set_active_language(DisplayLanguage lang) {
    Prefs::set(ACTIVE_LANGUAGE_PREFS_KEY, (int)lang);
}

static DisplayLanguage lang_map_language = DSPL_LANG_INVALID;
static EXT_RAM_ATTR std::map<const std::string, std::string> lang_map = {};

static void _load_lang_map_if_needed() {
    DisplayLanguage lang =  active_language();
    if(lang_map_language == lang) return;

    const char * filename = nullptr;
    switch(lang) {
        case DSPL_LANG_RU:
            filename = LANG_DIR_PREFIX "/ru.lang";
            break;
        case DSPL_LANG_JA:
            filename = LANG_DIR_PREFIX "/ja.lang";
            break;
        case DSPL_LANG_EN:
        default:
            filename = LANG_DIR_PREFIX "/en.lang";
            break;
    }

    File f = LittleFS.open(filename, "r");
    if(!f) {
        ESP_LOGE(LOG_TAG, "Loading %s failed! No such file? Pretending nothing happened.", filename);
        lang_map_language = lang;
        return;
    }

    JsonDocument content;
    DeserializationError error = deserializeJson(content, f);
    if(error) {
        ESP_LOGE(LOG_TAG, "Parsing %s failed: %s", filename, error.c_str());
        lang_map_language = lang;
        return;
    }

    JsonObject root = content.as<JsonObject>();
    lang_map.clear();
    int i = 0;

    for (JsonPair kv : root) {
        std::string val = kv.value().as<std::string>();
        lang_map[kv.key().c_str()] = val;
        i++;
    }

    lang_map_language = lang;
    ESP_LOGI(LOG_TAG, "Loaded language ID=%i, Entries: %i", lang_map_language, i);
}

const std::string localized_string(const std::string& key) {
    _load_lang_map_if_needed();

    if(lang_map.count(key)) {
        return lang_map.at(key);
    } else {
        return key;
    }
}
