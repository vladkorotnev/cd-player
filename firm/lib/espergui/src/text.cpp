#include <esper-gui/text.h>
#include <esper-gui/graphics.h>
#include <esper-core/miniz_ext.h>
#include <esp_heap_caps.h>
#include <esp32-hal-log.h>
#include <cerrno>
#include <algorithm>
#include <vector>

static char LOG_TAG[] = "FONT";

bool _EGStr_utf8_continuation_get(const char ** ptr, char32_t * dst) {
    if((**ptr & 0b11000000) != 0b10000000) {
        ESP_LOGE(LOG_TAG, "Malformed UTF8 sequence at 0x%x: expected continuation, got 0x%02x", *ptr, **ptr);
        return false;
    }
    *dst <<= 6;
    *dst |= (**ptr & 0b00111111);
    (*ptr)++;
    return true;
}

char16_t EGStr_utf8_iterate(const char ** ptr) {
    if(*ptr == 0) return 0;

    char32_t val = **ptr;

    if(val <= 0x7F) {
        // 1 byte case
        (*ptr)++;
    } else if((val & 0b11100000) == 0b11000000) {
        // 2 byte case
        val &= 0b00011111;
        (*ptr)++;
        if(!_EGStr_utf8_continuation_get(ptr, &val)) return 0;
    } else if((val & 0b11110000) == 0b11100000) {
        // 3 byte case
        val &= 0b00001111;
        (*ptr)++;
        if(!_EGStr_utf8_continuation_get(ptr, &val)) return 0;
        if(!_EGStr_utf8_continuation_get(ptr, &val)) return 0;
    } else if((val & 0b11111000) == 0b11110000) {
        // 4 byte case
        val &= 0b00000111;
        (*ptr)++;
        if(!_EGStr_utf8_continuation_get(ptr, &val)) return 0;
        if(!_EGStr_utf8_continuation_get(ptr, &val)) return 0;
        if(!_EGStr_utf8_continuation_get(ptr, &val)) return 0;
    } else {
        ESP_LOGE(LOG_TAG, "Unsupported UTF8 sequence at 0x%x (value = 0x%02x)", *ptr, **ptr);
        return 0;
    }

    return (val & 0xFFFF);
}

namespace Fonts {
    namespace MoFo {
        const uint16_t MONOFONT_CUR_VERSION = 0x0001;
        const uint32_t MONOFONT_MAGIC_HDR = 0x6F466F4D; // 'MoFo', MonoFont little endian
        const uint32_t MONOFONT_MAGIC_RANGES = 0x73676E52; // 'Rngs' Range table marker
        const uint32_t MONOFONT_MAGIC_RANGES_DEFL = 0x5A676E52; // 'RngZ' Range table marker
        const uint32_t MONOFONT_MAGIC_BITMAP = 0x73504D42; // 'BMPs' Bitmap section marker
        const uint32_t MONOFONT_MAGIC_BITMAP_DEFL = 0x5A504D42; // 'BMPZ' Deflated Bitmap section marker

        typedef struct __attribute__((packed)) mofo_header {
            const uint32_t magic;
            const uint16_t version;
            const Encoding encoding;
            const EGBufferFormat glyph_format;
            const char16_t cursor_character; //<- unused here
            const char16_t invalid_character;
            const uint8_t width;
            const uint8_t height;
        } mofo_header_t;
        
        typedef struct __attribute__((packed)) mofo_section {
            const uint32_t magic;
            const uint32_t size;
            union {
                const uint32_t real_size;
                const uint32_t item_count;
            }; // uncompressed size for BMPZ or RngZ, count of items for Rngs
        } mofo_section_t;

        bool LoadFromHandle(FILE * f, Fonts::Font * dest) {
            dest->valid = false;

            size_t r = 0;
            mofo_header_t head = { 0 };
            mofo_section_t sect = { 0 };
            long pos = ftell(f);
            r = fread(&head, 1, sizeof(mofo_header_t), f);
            if(r != sizeof(mofo_header_t)) {
                ESP_LOGE(LOG_TAG, "Header read error, wanted %i bytes, got %i", sizeof(mofo_header_t), r);
                return false;
            }
        
            if(head.magic != MONOFONT_MAGIC_HDR) {
                ESP_LOGE(LOG_TAG, "Expected header magic to be 0x%x, got 0x%x", MONOFONT_MAGIC_HDR, head.magic);
                return false;
            }
        
            if(head.version != MONOFONT_CUR_VERSION) {
                ESP_LOGE(LOG_TAG, "Expected header version to be 0x%x, got 0x%x", MONOFONT_CUR_VERSION, head.version);
                return false;
            }
        
            dest->encoding = head.encoding;
            dest->glyph_format = head.glyph_format;
            dest->invalid_character = head.invalid_character;
            dest->size.width = head.width;
            dest->size.height = head.height;
        
            pos = ftell(f);
            r = fread(&sect, 1, sizeof(mofo_section_t), f);
            if(r != sizeof(mofo_section_t)) {
                ESP_LOGE(LOG_TAG, "Section read error, wanted %i bytes, got %i", sizeof(mofo_section_t), r);
                return false;
            }
        
            if(sect.magic != MONOFONT_MAGIC_RANGES && sect.magic != MONOFONT_MAGIC_RANGES_DEFL) {
                ESP_LOGE(LOG_TAG, "Expected range table section at 0x%x (magic to be 0x%x or 0x%x, got 0x%x)", pos, MONOFONT_MAGIC_RANGES, MONOFONT_MAGIC_RANGES_DEFL, sect.magic);
                return false;
            }
        
            Range * ranges = (Range*) heap_caps_malloc(
                sect.size,
                MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM
            );

            if(ranges == nullptr) {
                ESP_LOGE(LOG_TAG, "OOM allocating range table");
                return false;
            }
        
            size_t total = 0;
            while(total < sect.size && r > 0) {
                r = fread(&((uint8_t*) ranges)[total], 1, sect.size - total, f);
                total += r;
            }
        
            if(total != sect.size) {
                ESP_LOGE(LOG_TAG, "Underrun reading range table: expected %u bytes but got only %u", sect.size, total);
                free(ranges);
                return false;
            }
        
            if(sect.magic == MONOFONT_MAGIC_RANGES_DEFL) {
                ESP_LOGI(LOG_TAG, "Decompressing RngZ");
                ranges = (Range *) decompress_emplace(ranges, sect.size, sect.real_size);
                if(ranges == nullptr) return false;
                dest->range_count =  (sect.real_size / sizeof(Range));
            } else {
                dest->range_count = sect.item_count;
            }
        
            dest->ranges = ranges;
            for(int i = 0; i < dest->range_count; i++) {
                const Range * r = &dest->ranges[i];
                ESP_LOGV(LOG_TAG, "  - Glyph range: start = 0x%x, end = 0x%x, bitmap offset = 0x%x", r->start, r->end, r->data_offset);
            }
        
            pos = ftell(f);
            r = fread(&sect, 1, sizeof(mofo_section_t), f);
            if(r != sizeof(mofo_section_t)) {
                ESP_LOGE(LOG_TAG, "Section read error, wanted %i bytes, got %i", sizeof(mofo_section_t), r);
                return false;
            }
        
            if(sect.magic != MONOFONT_MAGIC_BITMAP && sect.magic != MONOFONT_MAGIC_BITMAP_DEFL) {
                ESP_LOGE(LOG_TAG, "Expected bitmap table section at 0x%x (magic to be 0x%x or 0x%x, got 0x%x)", pos, MONOFONT_MAGIC_BITMAP, MONOFONT_MAGIC_BITMAP_DEFL, sect.magic);
                return false;
            }
        
            uint8_t * bitmap = (uint8_t*) heap_caps_malloc(
                sect.size,
                MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM
            );
            if(bitmap == nullptr) {
                ESP_LOGE(LOG_TAG, "OOM allocating bitmap table");
                free(ranges);
                return false;
            }
        
            total = 0;
            while(total < sect.size && r > 0) {
                r = fread(&bitmap[total], 1, sect.size - total, f);
                total += r;
            }
            
            if(total != sect.size) {
                ESP_LOGE(LOG_TAG, "Underrun reading bitmap table: expected %u bytes but got only %u", sect.size, total);
                free(bitmap);
                free(ranges);
                return false;
            }
        
            if(sect.magic == MONOFONT_MAGIC_BITMAP_DEFL) {
                ESP_LOGI(LOG_TAG, "Decompressing BMPZ");
                bitmap = (uint8_t*) decompress_emplace(bitmap, sect.size, sect.real_size);
                if(bitmap == nullptr) {
                    free(ranges);
                    return false;
                }
            }
        
            dest->data = bitmap;
            dest->valid = true;
        
            ESP_LOGI(LOG_TAG, "Got font: encoding=%x, glyphfmt=%x, cursor=%x, invalid=%x, w=%u, h=%u, range_cnt=%u, data=0x%x, ranges=0x%x", dest->encoding, dest->glyph_format, dest->cursor_character, dest->invalid_character, dest->size.width, dest->size.height, dest->range_count, dest->data, dest->ranges);
            return true;
        }

        bool Load(const char * path, Fonts::Font * dest) {
            ESP_LOGI(LOG_TAG, "Load %s", path);
            FILE * f = nullptr;
            f = fopen(path, "rb");
            if(!f) {
                ESP_LOGE(LOG_TAG, "File open error: %s (%i, %s)", path, errno, strerror(errno));
                return false;
            }
        
            fseek(f, 0, SEEK_SET);
            bool rslt = LoadFromHandle(f, dest);
            fclose(f);
            return rslt;
        }
    }

    const EGRawGraphBuf EGFont_glyph_data_ptr(const Font* font, char16_t glyph) {
        if(font == nullptr || !font->valid) return nullptr;

        const uint8_t * glyph_ptr = nullptr;
        int low = 0;
        int high = font->range_count - 1;
        while(low <= high) {
            int mid = low + (high - low) / 2;
            const Range * range = &font->ranges[mid];
            if(glyph >= range->start && glyph <= range->end) {
                int idx = 0;
                switch(font->glyph_format) {
                    case EG_FMT_HORIZONTAL:
                        idx = range->data_offset + (glyph - range->start) * std::max((font->size.width / 8), 1u) * font->size.height;
                        break;
                    case EG_FMT_NATIVE:
                    default:
                        idx = range->data_offset + (glyph - range->start) * std::max((font->size.height / 8), 1u) * font->size.width;
                        break;
                }
                glyph_ptr = &font->data[idx];
                break;
            } else if (glyph < range->start) {
                high = mid - 1;
            } else {
                low = mid + 1;
            }
        }
        return (const EGRawGraphBuf) glyph_ptr;
    }

    static std::vector<Font> font_registry = {};

    void EGFont_register(const Font f) {
        font_registry.push_back(f);
    }

    const Font FallbackWildcard(unsigned int height) {
        return Font {
            .valid = false,
            .encoding = Encoding::FONT_ENCODING_BESPOKE_ASCII,
            .glyph_format = EG_FMT_NATIVE,
            .cursor_character = 0,
            .invalid_character = 0,
            .size = {8, height},
            .range_count = 0,
            .data = nullptr,
            .ranges = nullptr
        };
    }

    const EGGraphBuf EGFont_glyph(const Font* font, char16_t glyph, bool allow_fallback) {
        auto ptr = EGFont_glyph_data_ptr(font, glyph);
        if(ptr == nullptr && allow_fallback) {
            for(auto subst: font_registry) {
                if(subst.size.height == font->size.height) {
                    ptr = EGFont_glyph_data_ptr(&subst, glyph);
                    if(ptr) {
                        return EGGraphBuf {
                            .fmt = subst.glyph_format,
                            .size = subst.size,
                            .data = ptr
                        };
                    }
                }
            }
        }
        if(ptr == nullptr) {
            if(!font->valid) {
                return EGGraphBuf {
                    .fmt = EG_FMT_NATIVE,
                    .size = EGSizeZero,
                    .data = nullptr
                };
            }
            ptr = EGFont_glyph_data_ptr(font, font->invalid_character);
        }
        return EGGraphBuf {
            .fmt = font->glyph_format,
            .size = font->size,
            .data = ptr
        };
    }

    void EGFont_put_string(const Font * font, const char * utf_string, EGPoint location, EGGraphBuf * dst) {
        while(char16_t ch = EGStr_utf8_iterate(&utf_string)) {
            auto tmp = EGFont_glyph(font, ch);
            EGBlitBuffer(dst, location, &tmp);
            location.x += tmp.size.width;
            if(location.x >= dst->size.width) break;
        }
    }
}
