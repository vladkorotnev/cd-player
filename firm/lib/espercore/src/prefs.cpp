#include <esper-core/prefs.h>
#include "nvs_flash.h"
#include <Preferences.h>
#include <vector>

static char LOG_TAG[] = "PREF";
static Preferences * store = nullptr;
static constexpr const char * STORE_DOMAIN = "espercdp";

namespace Prefs {
        static Preferences * get_store() {
            if(store == nullptr) {
                ESP_LOGD(LOG_TAG, "Initialize");
                store = new Preferences();
                nvs_flash_init(); // <- sometimes settings are being checked before Arduino finished initializing, so we need to bring NVS up on our own
                if(!store->begin(STORE_DOMAIN)) {
                    delete store;
                    store = nullptr;
                }
            }
            return store;
        }

        void reset_all() {
            get_store()->clear();
        }

        template <> int get(Key<int> key) {
            auto store = get_store();
            if(store->getType(key.first) == PreferenceType::PT_I32) {
                return store->getInt(key.first);
            } else {
                return key.second;
            }
        }

        template <> void set(Key<int> key, const int& val) {
            get_store()->putInt(key.first, val);
        }

        template <> bool get(Key<bool> key) {
            auto store = get_store();
            if(store->isKey(key.first)) {
                return store->getBool(key.first);
            } else {
                return key.second;
            }
        }

        template <> void set(Key<bool> key, const bool& val) {
            get_store()->putBool(key.first, val);
        }

        template <> std::string get(Key<std::string> key) {
            auto store = get_store();
            if(store->getType(key.first) == PreferenceType::PT_STR) {
                return store->getString(key.first).c_str(); //<- arduino string to c string to cpp string, duh
            } else {
                return key.second;
            }
        }

        template <> void set(Key<std::string> key, const std::string& val) {
            get_store()->putString(key.first, val.c_str());
        }

        template <> std::vector<uint8_t> get(Key<std::vector<uint8_t>> key) {
            auto store = get_store();
            if(store->getType(key.first) == PreferenceType::PT_BLOB) {
                std::vector<uint8_t> vec = {};
                vec.resize(store->getBytesLength(key.first));
                store->getBytes(key.first, vec.data(), vec.capacity());
                return vec;
            } else {
                return key.second;
            }
        }

        template <typename DataType> void erase(Key<DataType> key) {
            get_store()->remove(key.first);
        }
};