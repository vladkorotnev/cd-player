#pragma once
#include <stdint.h>
#include <cstring>

/// @brief Hardware surface point
struct EGPoint {
    int x;
    int y;
};

/// @brief Hardware surface size
struct EGSize {
    unsigned int width;
    unsigned int height;
};

struct EGRect {
    EGPoint origin;
    EGSize size;
};

const EGPoint EGPointZero = {0, 0};
const EGSize EGSizeZero = {0, 0};
const EGRect EGRectZero = {EGPointZero, EGSizeZero};
bool EGPointEqual(const EGPoint& a, const EGPoint& b);
bool EGSizeEqual(const EGSize& a, const EGSize& b);
bool EGRectEqual(const EGRect& a, const EGRect& b);
