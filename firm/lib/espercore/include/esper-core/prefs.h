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
    static const Prefs::Key<std::string> NTP_SERVER {"ntp_srv", "pool.ntp.org"};
};