#pragma once
#include <esper-core/prefs.h>

namespace NetRadio {
    const Prefs::Key<std::string> NamePrefsKeyForIndex(int idx);
    const Prefs::Key<std::string> URLPrefsKeyForIndex(int idx);
    const std::string URLForIndex(int idx);
    const std::string NameForIndex(int idx);
};
