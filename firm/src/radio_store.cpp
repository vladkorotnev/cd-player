#include <radio_store.h>
#include <esper-core/prefs.h>
#include <vector>

namespace NetRadio {
    static const std::vector<std::pair<std::string, std::string>> default_radios = { 
        {"Happy Hardcore", "u1.happyhardcore.com"},
        {"Provoda.ch", "station.waveradio.org/provodach"},
        {"Sovietwave", "station.waveradio.org/soviet"},
        {"Nightwave Plaza", "radio.plaza.one/mp3"},
        {"Keygen FM", "stream.keygen-fm.ru:8082"},
        {"Radio Cafe", "on.radio-cafe.ru:6045"},
    };

    const Prefs::Key<std::string> NamePrefsKeyForIndex(int idx) {
        return {"netr_n"+std::to_string(idx), default_radios[idx].first};
    }

    const Prefs::Key<std::string> URLPrefsKeyForIndex(int idx) {
        return {"netr_u"+std::to_string(idx), default_radios[idx].second};
    }

    const std::string URLForIndex(int idx) {
        return Prefs::get(URLPrefsKeyForIndex(idx));
    }

    const std::string NameForIndex(int idx) {
        return Prefs::get(NamePrefsKeyForIndex(idx));
    }
};
