#pragma once
#include <esper-gui/graphics.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

// NB: this is mostly ported from PIS-OS, just changing the code style to match the project
char16_t EGStr_utf8_iterate(const char ** ptr);
size_t EGStr_utf8_strlen(const char * str);

namespace Fonts {
    enum Encoding : uint8_t {
        /// @brief ASCII or some bespoke encoding (e.g. graphical fonts)
        FONT_ENCODING_BESPOKE_ASCII = 0,
        /// @brief UTF-16 codepoints
        FONT_ENCODING_UTF16,
    };

    struct __attribute__((packed)) Range {
        /// @brief The first character included in the range
        char16_t start;
        /// @brief The last character included in the range
        char16_t end;
        /// @brief Offset in the data block where the characters are uniformly, contiguously located
        size_t data_offset;
    };

    struct Font {
        bool valid;
        /// @brief In which encoding are the character ranges specified?
        Encoding encoding;
        /// @brief In which format are the glyphs stored?
        EGBufferFormat glyph_format;
        /// @brief The character that best suits the cursor role in the font
        char16_t cursor_character;
        /// @brief The character to draw when an unknown character is requested
        char16_t invalid_character;
        /// @brief Size of the character in pixels
        EGSize size;
        /// @brief Count of character ranges in the font
        uint32_t range_count;
        /// @brief Bitmap data of the fonts, laid out horizontally, aligned towards LSB.
        /// I.e. a 4x5 font will have 5 bytes per character, with each byte representing a single horizontal row of pixels, with bits counted M-to-L and aligned towards the LSB.
        const uint8_t* data;
        /// @brief Character ranges, ordered low to high
        const Range* ranges;
    };

    namespace MoFo {
        bool LoadFromHandle(FILE * f, Fonts::Font * dest);
        bool Load(const char * path, Fonts::Font * dest);
    }

    /// @brief Faux font to allow fallback to any 16px tall font. Rendering with it will cause a crash.
    extern const Font * FallbackWildcard16px;
    /// @brief Faux font to allow fallback to any 16px tall font. Rendering with it will cause a crash.
    extern const Font * FallbackWildcard12px;
    /// @brief Faux font to allow fallback to any 8px tall font. Rendering with it will cause a crash.
    extern const Font * FallbackWildcard8px;
    /// @brief Tiny 4x5px font containing only digits, minus and colon
    extern const Font * TinyDigitFont;

    void EGFont_register(const Font);
    const EGGraphBuf EGFont_glyph(const Font* font, char16_t glyph, bool allow_fallback = true);
    void EGFont_put_string(const Font * font, const char * utf_string, EGPoint location, EGGraphBuf * dst);
    EGSize EGFont_measure_string(const Font * font, const char * utf_string);
}
