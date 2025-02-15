#include <esper-gui/graphics.h>
#include <algorithm>
#include <esp32-hal-log.h>

static const char LOG_TAG[] = "GRAPH";

bool EGPointEqual(const EGPoint& a, const EGPoint& b) {
    return a.x == b.x && a.y == b.y;
}

bool EGSizeEqual(const EGSize& a, const EGSize& b) {
    return a.width == b.width && a.height == b.height;
}

bool EGRectEqual(const EGRect& a, const EGRect& b) {
    return EGPointEqual(a.origin, b.origin) && EGSizeEqual(a.size, b.size);
}

void EGBlitNative2Native(EGGraphBuf * dst, const EGPoint& location, const EGGraphBuf * src) {
    size_t dst_stride = dst->size.height/8;
    size_t src_stride = std::max(src->size.height/8, 1u);

    size_t src_start_col = std::max(0, -location.x);
    int src_start_row = std::max(0, -location.y);

    for(int src_col = src_start_col; src_col < std::min(src->size.width, (dst->size.width - std::max(0, location.x))); src_col++) {
        for(int src_row = src_start_row; src_row < std::min(src->size.height, (dst->size.height - std::max(0, location.y))); src_row++) {
            size_t src_byte_idx = (src_col * src_stride) + (src_row / 8);
            int src_bit_idx = (src_row % 8);

            int dst_row = std::max(0, location.y) + src_row - src_start_row;
            size_t dst_byte_idx = ((location.x + src_col - src_start_col) * dst_stride) + (dst_row / 8);
            int dst_bit_idx = (dst_row % 8);

            dst->data[dst_byte_idx] &= ~(0x80 >> dst_bit_idx);
            dst->data[dst_byte_idx] |= ((src->data[src_byte_idx] & (0x80 >> src_bit_idx)) != 0) << (7 - dst_bit_idx);
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