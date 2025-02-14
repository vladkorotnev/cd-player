#include <esper-gui/graphics.h>
#include <algorithm>
#include <esp32-hal-log.h>

static const char LOG_TAG[] = "GRAPH";

bool EGPointEqual(const EGPoint& a, const EGPoint& b) {
    return a.x == a.x && a.y == b.y;
}

bool EGSizeEqual(const EGSize& a, const EGSize& b) {
    return a.width == b.width && a.height == b.height;
}

bool EGRectEqual(const EGRect& a, const EGRect& b) {
    return EGPointEqual(a.origin, b.origin) && EGSizeEqual(a.size, b.size);
}

void EGBlitNative2Native(EGGraphBuf * dst, const EGPoint& location, const EGGraphBuf * src) {
    size_t stride = dst->size.height/8;
    size_t view_stride = std::max(src->size.height/8, 1u);
    uint8_t row_first = std::max(0, location.y) / 8;
    uint8_t row_last = (location.y + src->size.height - 1) / 8;
    uint8_t bit_offs = std::max(0, location.y) % 8;

    for(int col = 0; col < std::min(src->size.width, (dst->size.width - location.x)); col++) {
        for(int row = 0; row < stride; row++) {
            if(row >= row_first && row <= row_last) {
                uint8_t dst_byte = 0;
                uint8_t dst_mask = 0;
                size_t surf_idx = ((col + std::max(0, -location.x)) * view_stride) + (row - row_first) + std::max(0, -location.y);
                if(row > row_first && bit_offs > 0) {
                    dst_byte |= (src->data[surf_idx - 1] << (8 - bit_offs));
                    dst_mask |= (0xFF << (8 - bit_offs));
                }
                dst_byte |= (src->data[surf_idx] >> bit_offs);
                dst_mask |= (0xFF >> bit_offs);
                int px_remain = src->size.height - (row - row_first)*(8 - bit_offs) - (row > row_first ? bit_offs : 0);
                if(px_remain < 8 - bit_offs) {
                    // we have less pixels remaining than we can fit in this byte, trim mask accordingly to not clip whatever we have in the same byte
                    dst_mask &= 0xFF << (8 - (px_remain + bit_offs));
                }

                size_t fb_idx = (location.x + col) * stride + row;
                dst->data[fb_idx] &= ~dst_mask;
                dst->data[fb_idx] |= (dst_byte & dst_mask);
            }
        }
    }
}

void EGBlitHorizontal2Native(EGGraphBuf * dst, const EGPoint& location, const EGGraphBuf * src) {
    // Does this have ass performance? Who knows...
    size_t view_stride = std::max(src->size.width/8, 1u);

    uint8_t tmp_col_buf[std::max(src->size.height/8, 1u)];
    EGGraphBuf tmp_col_src = {
        .fmt = EG_FMT_NATIVE,
        .size = { .width = 1, .height = src->size.height },
        .data = tmp_col_buf
    };

    for(int col = 0; col < std::min(src->size.width, (dst->size.width - location.x)); col++) {
        memset(tmp_col_buf, 0, sizeof(tmp_col_buf));
        for(int src_row = 0; src_row < src->size.height; src_row++) {
            tmp_col_buf[src_row / 8] |= ((src->data[src_row * view_stride + col / 8] & (0x80 >> (col % 8))) != 0) << (7 - (src_row % 8));
        }

        // - Ну вот как по-французски "Иди сюда?"
        // - "Вьен иси"
        // - Хорошо, а как ты скажешь по-французски "Иди туда?"
        // - Ну я пойду туда и скажу "вьен иси!"
        // (шоу Бенни Хилла)
        EGBlitNative2Native(dst, { location.x + col, location.y }, &tmp_col_src );
    }
}

void EGBlitBuffer(EGGraphBuf * dst, const EGPoint& location, const EGGraphBuf * src) {
    switch(dst->fmt) {
        case EG_FMT_NATIVE: 
            switch(src->fmt) {
                case EG_FMT_NATIVE:
                    EGBlitNative2Native(dst, location, src);
                    break;
                case EG_FMT_HORIZONTAL:
                    EGBlitHorizontal2Native(dst, location, src);
                    break;

                default:
                    ESP_LOGE(LOG_TAG, "Unsupported conversion fmt=%i to fmt=%i", src->fmt, dst->fmt);
                    break;
            }
            break;

        default:
            ESP_LOGE(LOG_TAG, "Unsupported conversion fmt=%i to fmt=%i", src->fmt, dst->fmt);
            break;
    }
}