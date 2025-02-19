#pragma once
#include <esper-gui/views/framework.h>
#include <locale>
#include <codecvt>
#include <string>

/// @brief A label that auto-sizes and wraps text
class LyricLabel: public UI::View {
public:
    EGSize str_size = EGSizeZero;

    LyricLabel(EGRect frame): View(frame) {
        set_value("");
    }

    void set_value(std::string str) {
        if(str != value) {
            value = str;
            typeset();
            set_needs_display();
        }
    }

    void render(EGGraphBuf * buf) override {
        EGPoint origin = EGPointZero;
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        for(auto& line: lines) {
            auto utf8 = converter.to_bytes(line);
            Fonts::EGFont_put_string(font_to_use, utf8.c_str(), origin, buf);
            origin.y += font_to_use->size.height;
        }

        View::render(buf);
    }

protected:
    void typeset() {
        if(!try_typesetting_with_font(Fonts::FallbackWildcard16px, value.c_str())) {
            // 12px font we have only looks good with CJK, don't typeset other scripts with it
            if(!string_has_cjk(value.c_str()) || !try_typesetting_with_font(Fonts::FallbackWildcard12px, value.c_str())) {
                try_typesetting_with_font(Fonts::FallbackWildcard8px, value.c_str());
            }
        }
    }

    bool try_typesetting_with_font(const Fonts::Font* font, const char * val_utf) {
        lines.clear();
        font_to_use = font;
        int str_len = EGStr_utf8_strlen(val_utf);
        ESP_LOGD(LOG_TAG, "Formatting a line of %i chars with a font of %i x %i", str_len, font->size.width, font->size.height);

        std::wstring buffer = std::wstring();
        bool just_wrapped = false;
        int line_len_px = 0;

        while(wchar_t ch = EGStr_utf8_iterate(&val_utf)) {
            // for now, dumb wrapping without word wrap, but omit spaces after line break and double line breaks
            if(!just_wrapped || (ch != ' ' && ch != '　' && ch != '\n')) {
                buffer.push_back(ch);
                auto glyph = Fonts::EGFont_glyph(font, ch);
                if(glyph.size.width == 0 && glyph.size.height == 0) {
                    // oops! no such glyph in the font, cannot typeset that way
                    return false;
                }
                line_len_px += glyph.size.width;
                just_wrapped = false;
            }

            str_len--;
            ESP_LOGD(LOG_TAG, "Remain px = %i, Line len px = %i, Str remain = %i", (frame.size.width - line_len_px), line_len_px, str_len);
            if(frame.size.width - line_len_px < font->size.width) {
                // one more character won't fit, break lines
                lines.push_back(buffer);
                ESP_LOGD(LOG_TAG, "Wrapped line %i: %hs", lines.size(), buffer.c_str());
                just_wrapped = true;
                buffer = std::wstring();
                line_len_px = 0;
                if(frame.size.height - lines.size() * font->size.height < font->size.height) {
                    // one more line won't fit, end of story
                    ESP_LOGD(LOG_TAG, "Could not fit %i more chars", str_len);
                    return (str_len == 0);
                }
            }
        }

        lines.push_back(buffer);
        ESP_LOGD(LOG_TAG, "Wrapped final line %i: %hs", lines.size(), buffer.c_str());

        return true;
    }

    bool string_has_cjk(const char * str) {
        while(char16_t ch = EGStr_utf8_iterate(&str)) {
            if( 
                (ch >= 0x4E00 && ch <= 0x9FFF) || // CJK Unified Ideographs
                (ch >= 0x3040 && ch <= 0x30FF) || // hiragana + katakana
                (ch >= 0x31F0 && ch <= 0x31FF) || // Katakana Phonetic Extensions
                (ch >= 0xFF00 && ch <= 0xFFEF) || // Halfwidth and Fullwidth Forms
                (ch >= 0x1100 && ch <= 0x11FF) || // Hangul Jamo
                (ch >= 0x3130 && ch <= 0x318F) || // Hangul Compatibility Jamo
                (ch >= 0xAC00 && ch <= 0xD7AF)    // Hangul Syllables
            )
                return true;
        }
        return false;
    }

    const Fonts::Font* font_to_use = Fonts::FallbackWildcard16px;
    std::string value = "";
    std::vector<std::wstring> lines = {};
    const char * LOG_TAG = "Typeset";
};