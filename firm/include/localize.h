#pragma once
#include <string>

enum DisplayLanguage: int {
    DSPL_LANG_EN = 0,
    DSPL_LANG_RU,
    DSPL_LANG_JA,
    DSPL_LANG_HU,
    DSPL_LANG_DE,
    DSPL_LANG_NL,

    DSPL_LANG_INVALID
};

DisplayLanguage active_language();
const std::string language_name(DisplayLanguage);
void set_active_language(DisplayLanguage);
const std::string localized_string(const std::string& key);
