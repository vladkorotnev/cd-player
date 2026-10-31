#pragma once

#include <cJSON.h>
#include <string>

namespace WebPreferences {
    constexpr const char * PASSWORD_MASK = "********";

    enum class WriteResult { saved, unchanged, unknown_key, invalid_value };

    /// Caller owns the returned cJSON object.
    cJSON * read_all();
    cJSON * read_one(const std::string& name);
    WriteResult write_one(const std::string& name, const cJSON * value);
}
