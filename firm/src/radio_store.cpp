#include <radio_store.h>
#include <esper-core/prefs.h>

namespace NetRadio {
    const Prefs::Key<std::string> NamePrefsKeyForIndex(int idx) {
        return {"netr_n"+std::to_string(idx), ""};
    }

    const Prefs::Key<std::string> URLPrefsKeyForIndex(int idx) {
        return {"netr_u"+std::to_string(idx), ""};
    }

    const std::string URLForIndex(int idx) {
        return Prefs::get(URLPrefsKeyForIndex(idx));
    }

    const std::string NameForIndex(int idx) {
        return Prefs::get(NamePrefsKeyForIndex(idx));
    }
};
