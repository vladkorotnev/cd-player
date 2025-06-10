#include <utils.h>

std::string format_bytes(size_t bytes) {
    std::string space_str;

    if (bytes > 1024 * 1024) {
        space_str = std::to_string(bytes / 1024 / 1024) + "M";
    }
    else if (bytes >= 1024) {
        space_str = std::to_string(bytes / 1024) + "K";
    }
    else {
        space_str = std::to_string(bytes) + "B";
    }
    return space_str;
}

std::string format_bits(size_t bits) {
    std::string space_str;

    if (bits > 1000 * 1000) {
        space_str = std::to_string(bits / 1000 / 1000) + "Mb";
    }
    else if (bits >= 1000) {
        space_str = std::to_string(bits / 1000) + "Kb";
    }
    else {
        space_str = std::to_string(bits) + "b";
    }
    return space_str;
}

std::string format_bytes_as_bits(size_t bytes) {
    return format_bits(bytes * 8);
}