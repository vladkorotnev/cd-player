#pragma once
#include <utility>
#include <string>

namespace Prefs {
    template <typename DataType> using Key = std::pair<const std::string, DataType>;

    template <typename DataType> DataType get(Key<DataType> key);
    template <typename DataType> void set(Key<DataType> key, const DataType& val);
    template <typename DataType> void erase(Key<DataType> key);
    void reset_all();
};

namespace Core::PrefsKey {
    static const Prefs::Key<std::string> PREFS_KEY_NTP_SERVER {"ntp_srv", "pool.ntp.org"};
    
    #ifndef WIFI_NAME
    #define WIFI_NAME ""
    #endif
    #ifndef WIFI_PASS
    #define WIFI_PASS ""
    #endif

    static const Prefs::Key<std::string> PREFS_KEY_WIFI_NAME{"wifi_ssid", WIFI_NAME};
    static const Prefs::Key<std::string> PREFS_KEY_WIFI_PASS{"wifi_pass", WIFI_PASS};
};