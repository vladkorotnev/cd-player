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
            animation_started_at = xTaskGetTickCount();
            last_animation_tick = animation_started_at;
            animation_active = true;
            set_needs_display();
        }
    }

    bool needs_display() override {
        if(animation_active) {
            TickType_t now = xTaskGetTickCount();
            if(now - animation_started_at >= pdMS_TO_TICKS(animation_duration_ms)) {
                animation_active = false;
                set_needs_display(); // erase the bottom edge after its final frame
            }
            else if(now - last_animation_tick >= pdMS_TO_TICKS(animation_frame_interval_ms)) {
                last_animation_tick = now;
                set_needs_display();
            }
        }

        return View::needs_display();
    }

    void render(EGGraphBuf * buf) override {
        EGPoint origin = EGPointZero;
        origin.y = std::max(0, (int)(frame.size.height/2 - (font_to_use->size.height * lines.size()) / 2));
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        for(auto& line: lines) {
            auto utf8 = converter.to_bytes(line);
            EGSize line_size = Fonts::EGFont_measure_string(font_to_use, utf8.c_str());
            origin.x = frame.size.width/2 - line_size.width/2;
            Fonts::EGFont_put_string(font_to_use, utf8.c_str(), origin, buf);
            origin.y += font_to_use->size.height;
        }

        draw_change_swipe(buf);
        
        View::render(buf);
    }

protected:
    void draw_change_swipe(EGGraphBuf * buf) {
        if(!animation_active || frame.size.width < 2 || frame.size.height < 2) return;

        const int width = frame.size.width;
        const int height = frame.size.height;
        const int elapsed_ms = pdTICKS_TO_MS(xTaskGetTickCount() - animation_started_at);
        const int side_length = std::min(animation_side_length, height - 1);

        if(elapsed_ms < animation_intro_ms) {
            // Reveal the top edge while short side rails grow down from it.
            const int rail_end = std::min(side_length,
                1 + elapsed_ms * side_length / animation_intro_ms);
            EGDrawLine(buf, {0, 0}, {0, rail_end});
            EGDrawLine(buf, {width - 1, 0}, {width - 1, rail_end});
            return;
        }

        if(elapsed_ms < animation_intro_ms + animation_travel_ms) {
            // Drop the top edge and move the fixed-length rails downward.
            const int travel_elapsed = elapsed_ms - animation_intro_ms;
            const int rail_end = side_length
                + travel_elapsed * (height - 1 - side_length) / animation_travel_ms;
            const int rail_start = rail_end - side_length;
            EGDrawLine(buf, {0, rail_start}, {0, rail_end});
            EGDrawLine(buf, {width - 1, rail_start}, {width - 1, rail_end});
            return;
        }

        if(elapsed_ms < animation_intro_ms + animation_travel_ms + animation_collapse_ms) {
            // Reveal the bottom edge and collapse the rails into it.
            const int collapse_elapsed = elapsed_ms - animation_intro_ms - animation_travel_ms;
            const int remaining = side_length
                - collapse_elapsed * side_length / animation_collapse_ms;
            EGDrawLine(buf, {0, height - 1 - remaining}, {0, height - 1});
            EGDrawLine(buf, {width - 1, height - 1 - remaining}, {width - 1, height - 1});
            return;
        }
    }

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
        std::wstring cur_word = std::wstring();
        int line_len_px = 0;
        int word_len_px = 0;
        EGGraphBuf last_space_glyph;
        wchar_t last_space_char = 0;

        while(wchar_t ch = EGStr_utf8_iterate(&val_utf)) {
            str_len--;
            if(ch != ' ' && ch != '\n' && ch != 0x3000 /* '　' jp space */ && ch != 0x3001 /* jp comma */ && ch != 0x3002 /* jp dot */) {
                cur_word.push_back(ch);
                auto glyph = Fonts::EGFont_glyph(font, ch);
                word_len_px += glyph.size.width;
            }
            else {
                // word boundary!
                // check if word and previous space will fit on current line
                if((frame.size.width - line_len_px) >= (word_len_px + (last_space_char == 0 ? 0 : last_space_glyph.size.width))) {
                    // it fits! push it in, including previous space
                    if(last_space_char != 0) {
                        buffer.push_back(last_space_char);
                        line_len_px += last_space_glyph.size.width;
                    }
                    buffer.append(cur_word);
                    line_len_px += word_len_px;
                }
                else {
                    // it doesn't fit!
                    // can we do more lines?
                    if((frame.size.height - lines.size() * font->size.height) < font->size.height) {
                        // one more line won't fit, end of story
                        ESP_LOGD(LOG_TAG, "Could not fit %i more chars", (str_len + cur_word.size()));
                        return ((str_len + cur_word.size()) == 0);
                    }

                    // store current line
                    lines.push_back(buffer);

                    // current word becomes start of next line
                    line_len_px = word_len_px;
                    buffer = cur_word;
                }

                cur_word = std::wstring();
                word_len_px = 0;
                // keep the current space for later
                last_space_glyph = Fonts::EGFont_glyph(font, ch);
                last_space_char = ch;
            }
        }

        if(cur_word.size() > 0) {
            // check if word will fit on current line
            if((frame.size.width - line_len_px) >= (word_len_px + (last_space_char == 0 ? 0 : last_space_glyph.size.width))) {
                // it fits! push it in, including previous space
                if(last_space_char != 0) {
                    buffer.push_back(last_space_char);
                    line_len_px += last_space_glyph.size.width;
                }
                buffer.append(cur_word);
                line_len_px += word_len_px;
            }
            else {
                // it doesn't fit!
                // can we do more lines?
                if((frame.size.height - lines.size() * font->size.height) < font->size.height) {
                    // one more line won't fit, end of story
                    ESP_LOGD(LOG_TAG, "Could not fit %i more chars", (str_len + cur_word.size()));
                    return ((str_len + cur_word.size()) == 0);
                }

                lines.push_back(buffer);

                line_len_px = word_len_px;
                buffer = cur_word;
            }
        }

        if(buffer.size() > 0) {
            if((frame.size.height - lines.size() * font->size.height) < font->size.height) {
                // last line didn't fit, directed by robert b. weide
                ESP_LOGD(LOG_TAG, "Could not fit %i more chars", (buffer.size()));
                return false;
            }

            lines.push_back(buffer);
        }

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
    static constexpr int animation_intro_ms = 120;
    static constexpr int animation_travel_ms = 240;
    static constexpr int animation_collapse_ms = 120;
    static constexpr int animation_duration_ms = animation_intro_ms
        + animation_travel_ms + animation_collapse_ms;
    static constexpr int animation_frame_interval_ms = 16;
    static constexpr int animation_side_length = 6;
    TickType_t animation_started_at = 0;
    TickType_t last_animation_tick = 0;
    bool animation_active = false;
    const char * LOG_TAG = "Typeset";
};
