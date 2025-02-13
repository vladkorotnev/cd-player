#pragma once
#include <stdint.h>
#include <cstring>

/// @brief Hardware surface point
struct EGPoint {
    uint16_t x;
    uint8_t y;
};

/// @brief Hardware surface size
struct EGSize {
    uint16_t width;
    uint8_t height;
};

struct EGRect {
    EGPoint origin;
    EGSize size;
};

bool EGPointEqual(const EGPoint& a, const EGPoint& b);
bool EGSizeEqual(const EGSize& a, const EGSize& b);
bool EGRectEqual(const EGRect& a, const EGRect& b);
