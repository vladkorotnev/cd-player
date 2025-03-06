#pragma once
#include <stdint.h>
#include <cstring>

typedef uint8_t* EGRawGraphBuf;

/// @brief Hardware surface point
struct EGPoint {
    int x;
    int y;
};

/// @brief Hardware surface size
struct EGSize {
    int width;
    int height;
};

struct EGRect {
    EGPoint origin;
    EGSize size;
};

enum EGBufferFormat: uint8_t {
    /// A horizontally laid out pixel buffer aligned to the left
    /// @details I.e. a 14x2 image will consist of 4 bytes. 0th one will contain the leftmost 6 pixels of the top row aligned towards MSB, 
    ///          1st one will contain the rightmost 8 pixels, 2nd one will contain the leftmost 6px of the bottom row, and so forth.
    EG_FMT_HORIZONTAL,
    /// A vertically laid out pixel buffer
    /// @details I.e. a 14x2 image will consist of 4 bytes. 0th one will contain the 8 pixels of the leftmost column with top left being MSB,
    ///          1st one will contain the next 6 pixels of the lefrmost column aligned towards MSB, 2nd one will contain the top 8px of the second column and so on.
    EG_FMT_NATIVE,
};

struct EGGraphBuf {
    EGBufferFormat fmt;
    EGSize size;
    EGRawGraphBuf data;
};

/// A constant image buffer
struct EGImage {
    EGBufferFormat format;
    EGSize size;
    const uint8_t * data;
};

const EGPoint EGPointZero = {0, 0};
const EGSize EGSizeZero = {0, 0};
const EGRect EGRectZero = {EGPointZero, EGSizeZero};
bool EGPointEqual(const EGPoint& a, const EGPoint& b);
bool EGSizeEqual(const EGSize& a, const EGSize& b);
bool EGRectEqual(const EGRect& a, const EGRect& b);
EGRect EGRectInset(EGRect, int dx, int dy);
void EGBlitBuffer(EGGraphBuf * dst, const EGPoint& location, const EGGraphBuf * src);
void EGDrawPixel(EGGraphBuf * dst, const EGPoint& location, bool state);
void EGDrawLine(EGGraphBuf * dst, const EGPoint& start, const EGPoint& end, bool state = true);
void EGDrawRect(EGGraphBuf * dst, const EGRect rect, bool filled = false, bool state = true);
void EGBufferInvert(EGGraphBuf * dst);
void EGBlitImage(EGGraphBuf * buf, const EGPoint location, const EGImage* image);
