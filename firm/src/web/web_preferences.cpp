#include <web/web_preferences.h>
#include <shared_prefs.h>
#include <radio_store.h>
#include <localize.h>
#include <esper-core/prefs.h>
#include <esper-gui/views/label.h>
#include <cstring>
#include <variant>
#include <vector>

namespace {
using BoolKey = Prefs::Key<bool>;
using IntKey = Prefs::Key<int>;
using StringKey = Prefs::Key<std::string>;

struct Preference {
    std::variant<BoolKey, IntKey, StringKey> key;
    bool password = false;
};

const std::vector<Preference>& keys() {
    static const std::vector<Preference> entries = [] {
        std::vector<Preference> result = {
            {PREFS_KEY_CD_MODE_INCLUDED},
            {PREFS_KEY_RADIO_MODE_INCLUDED},
            {PREFS_KEY_BLUETOOTH_MODE_INCLUDED},
            {PREFS_KEY_CD_LYRICS_ENABLED},
            {PREFS_KEY_CD_CACHE_META},
            {PREFS_KEY_CD_MUSICBRAINZ_ENABLED},
            {PREFS_KEY_CD_CDDB_ENABLED},
            {PREFS_KEY_CD_CDTEXT_ENABLED},
            {PREFS_KEY_CD_LRCLIB_ENABLED},
            {PREFS_KEY_CD_QQ_ENABLED},
            {PREFS_KEY_CD_NETEASE_ENABLED},
            {PREFS_KEY_CD_LASTFM_ENABLED},
            {PREFS_KEY_CD_LASTFM_USER},
            {PREFS_KEY_CD_LASTFM_PASS, true},
            {PREFS_KEY_CDDB_ADDRESS},
            {PREFS_KEY_CDDB_EMAIL},
            {PREFS_KEY_BT_NAME},
            {PREFS_KEY_BT_RECONNECT},
            {PREFS_KEY_BT_NEED_PIN},
            {PREFS_KEY_OTAFVU_ALLOWED},
            {PREFS_KEY_TELNET_LOGGER},
            {PREFS_KEY_WEB_REMOTE_ENABLED},
            {Core::PrefsKey::PREFS_KEY_NTP_SERVER},
            {Core::PrefsKey::PREFS_KEY_WIFI_NAME},
            {Core::PrefsKey::PREFS_KEY_WIFI_PASS, true},
            {PREFS_KEY_UI_LANGUAGE},
            {PREFS_KEY_UI_SCROLL_SPEED},
        };
        result.reserve(result.size() + 12);
        for(int i = 0; i < 6; ++i) {
            result.push_back({NetRadio::NamePrefsKeyForIndex(i)});
            result.push_back({NetRadio::URLPrefsKeyForIndex(i)});
        }
        return result;
    }();
    return entries;
}

const std::string& key_name(const Preference& preference) {
    return std::visit([](const auto& key) -> const std::string& { return key.first; }, preference.key);
}

const Preference * find(const std::string& name) {
    for(const auto& preference: keys()) {
        if(key_name(preference) == name) return &preference;
    }
    return nullptr;
}

cJSON * encode(const Preference& preference) {
    cJSON * item = cJSON_CreateObject();
    if(item == nullptr) return nullptr;
    cJSON_AddStringToObject(item, "key", key_name(preference).c_str());
    cJSON_AddBoolToObject(item, "password", preference.password);
    if(const auto * key = std::get_if<BoolKey>(&preference.key)) {
        cJSON_AddStringToObject(item, "type", "bool");
        cJSON_AddBoolToObject(item, "value", Prefs::get(*key));
    } else if(const auto * key = std::get_if<IntKey>(&preference.key)) {
        cJSON_AddStringToObject(item, "type", "int");
        cJSON_AddNumberToObject(item, "value", Prefs::get(*key));
    } else {
        const auto& skey = std::get<StringKey>(preference.key);
        std::string value = Prefs::get(skey);
        cJSON_AddStringToObject(item, "type", "string");
        cJSON_AddStringToObject(item, "value",
            preference.password && !value.empty() ? WebPreferences::PASSWORD_MASK : value.c_str());
    }
    return item;
}
}

namespace WebPreferences {
cJSON * read_all() {
    cJSON * result = cJSON_CreateArray();
    if(result == nullptr) return nullptr;
    for(const auto& preference: keys()) {
        cJSON * item = encode(preference);
        if(item == nullptr) {
            cJSON_Delete(result);
            return nullptr;
        }
        cJSON_AddItemToArray(result, item);
    }
    return result;
}

cJSON * read_one(const std::string& name) {
    const Preference * preference = find(name);
    return preference == nullptr ? nullptr : encode(*preference);
}

WriteResult write_one(const std::string& name, const cJSON * value) {
    const Preference * preference = find(name);
    if(preference == nullptr) return WriteResult::unknown_key;
    if(const auto * key = std::get_if<BoolKey>(&preference->key)) {
        if(!cJSON_IsBool(value)) return WriteResult::invalid_value;
        Prefs::set(*key, cJSON_IsTrue(value) == 1);
    } else if(const auto * key = std::get_if<IntKey>(&preference->key)) {
        if(!cJSON_IsNumber(value) || value->valuedouble != value->valueint)
            return WriteResult::invalid_value;
        int next = value->valueint;
        if(name == PREFS_KEY_UI_LANGUAGE.first && (next < DSPL_LANG_EN || next >= DSPL_LANG_INVALID))
            return WriteResult::invalid_value;
        if(name == PREFS_KEY_UI_SCROLL_SPEED.first && !UI::Label::is_valid_scroll_speed(next))
            return WriteResult::invalid_value;
        Prefs::set(*key, next);
        if(name == PREFS_KEY_UI_SCROLL_SPEED.first) UI::Label::set_scroll_speed(next);
    } else {
        if(!cJSON_IsString(value)) return WriteResult::invalid_value;
        const char * next = value->valuestring;
        if(next == nullptr || strlen(next) > 512) return WriteResult::invalid_value;
        for(const unsigned char * p = (const unsigned char *)next; *p; ++p) {
            if(*p < 0x20 || *p == 0x7f) return WriteResult::invalid_value;
        }
        if(preference->password && strcmp(next, PASSWORD_MASK) == 0) return WriteResult::unchanged;
        Prefs::set(std::get<StringKey>(preference->key), std::string(next));
    }
    return WriteResult::saved;
}
}
