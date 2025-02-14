#include <esper-gui/text.h>
#include <esper-core/miniz_ext.h>
#include <esp_heap_caps.h>
#include <esp32-hal-log.h>
#include <cerrno>

static char LOG_TAG[] = "FONT";

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
            const char16_t cursor_character;
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
            dest->cursor_character = head.cursor_character;
            dest->invalid_character = head.invalid_character;
            dest->width = head.width;
            dest->height = head.height;
        
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
        
            ESP_LOGI(LOG_TAG, "Got font: encoding=%x, glyphfmt=%x, cursor=%x, invalid=%x, w=%u, h=%u, range_cnt=%u, data=0x%x, ranges=0x%x", dest->encoding, dest->glyph_format, dest->cursor_character, dest->invalid_character, dest->width, dest->height, dest->range_count, dest->data, dest->ranges);
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
}