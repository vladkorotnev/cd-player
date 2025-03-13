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

const std::string language_name(DisplayLanguage lang) {
    switch(lang) {
        case DSPL_LANG_RU: return "Русский";
        case DSPL_LANG_JA: return "日本語";
        case DSPL_LANG_EN: return "English";
        case DSPL_LANG_HU: return "Magyar";
        case DSPL_LANG_DE: return "Deutsch";
        case DSPL_LANG_NL: return "Nederlands";
        default:
            return "???";
    }
}

static DisplayLanguage lang_map_language = DSPL_LANG_INVALID;
static EXT_RAM_ATTR std::map<const std::string, std::string> lang_map = {};

static bool _load_lang_map_if_needed() {
    DisplayLanguage lang =  active_language();
    if(lang_map_language == lang) return true;

    lang_map.clear(); // otherwise switching to "implicit"/native locale fails

    const char * filename = nullptr;
    switch(lang) {
        case DSPL_LANG_RU:
            filename = LANG_DIR_PREFIX "/ru.lang";
            break;
        case DSPL_LANG_JA:
            filename = LANG_DIR_PREFIX "/ja.lang";
            break;
        case DSPL_LANG_HU:
            filename = LANG_DIR_PREFIX "/hu.lang";
            break;
        case DSPL_LANG_DE:
            filename = LANG_DIR_PREFIX "/de.lang";
            break;
        case DSPL_LANG_NL:
            filename = LANG_DIR_PREFIX "/nl.lang";
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
        return false;
    }

    JsonDocument content;
    DeserializationError error = deserializeJson(content, f);
    if(error) {
        ESP_LOGE(LOG_TAG, "Parsing %s failed: %s", filename, error.c_str());
        lang_map_language = lang;
        return false;
    }

    JsonObject root = content.as<JsonObject>();
    int i = 0;

    for (JsonPair kv : root) {
        std::string val = kv.value().as<std::string>();
        lang_map[kv.key().c_str()] = val;
        i++;
    }

    lang_map_language = lang;
    ESP_LOGI(LOG_TAG, "Loaded language ID=%i, Entries: %i", lang_map_language, i);
    return true;
}

const std::string localized_string(const std::string& key) {
    if(!_load_lang_map_if_needed()) ESP_LOGW(LOG_TAG, "Failed to load map when translating [%s]", key.c_str());

    if(lang_map.count(key)) {
        return lang_map.at(key);
    } else {
        return key;
    }
}
