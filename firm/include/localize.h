#pragma once
#include <string>

enum DisplayLanguage: int {
    DSPL_LANG_INVALID = -1,
    
    DSPL_LANG_EN = 0,
    DSPL_LANG_RU = 1,
    DSPL_LANG_JA = 2
};

DisplayLanguage active_language();
const std::string language_name(DisplayLanguage);
void set_active_language(DisplayLanguage);
const std::string localized_string(const std::string& key);
