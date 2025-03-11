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
    size_t dst_stride = dst->size.height/8 + ((dst->size.height%8) != 0);
    size_t src_stride = src->size.height/8 + ((src->size.height%8) != 0);

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

void EGBufferInvert(EGGraphBuf * dst) {
    size_t byte_count;
    if(dst->fmt == EG_FMT_HORIZONTAL) {
        byte_count = dst->size.height * ((dst->size.width/8) + ((dst->size.width%8) != 0));
    } else {
        byte_count = dst->size.width * ((dst->size.height/8) + ((dst->size.height%8) != 0));
    }

    for(int i = 0; i < byte_count; i++) {
        dst->data[i] = ~dst->data[i];
    }
}

void EGBlitHorizontal2Native(EGGraphBuf * dst, const EGPoint& location, const EGGraphBuf * src) {
    // Does this have ass performance? Who knows...
    size_t view_stride = std::max(src->size.width/8, 1);

    uint8_t tmp_col_buf[src->size.height/8 + ((src->size.height%8) != 0)];
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

void EGBlitImage(EGGraphBuf * buf, const EGPoint location, const EGImage* image) {
    if(image != nullptr && image->data != nullptr) {
        const EGGraphBuf tmp_buf = {
            .fmt = image->format,
            .size = image->size,
            .data = (EGRawGraphBuf) image->data
        };
        EGBlitBuffer(buf, location, &tmp_buf);
    }
}

void EGDrawPixel(EGGraphBuf * dst, const EGPoint& location, bool state) {
    if(location.x < 0 || location.y < 0 || location.x >= dst->size.width || location.y >= dst->size.height) return;
    int byte;
    int bit;
    if(dst->fmt == EG_FMT_NATIVE) {
        size_t stride = std::max(1, dst->size.height / 8);
        byte = location.x * stride + location.y / 8;
        bit = location.y % 8;
    }
    else if(dst->fmt == EG_FMT_HORIZONTAL) {
        size_t stride = std::max(1, dst->size.width / 8);
        byte = location.y * stride + location.x / 8;
        bit = location.x % 8;
    }
    else {
        ESP_LOGE(LOG_TAG, "Unknown buffer format %i!!", dst->fmt);
        return;
    }

    if(!state)
        dst->data[byte] &= ~(0x80 >> bit);
    else
        dst->data[byte] |= (0x80 >> bit);
}

void EGDrawLine(EGGraphBuf * dst, const EGPoint& start, const EGPoint& end, bool state) {
    int x1 = start.x;
    int y1 = start.y;
    int x2 = end.x;
    int y2 = end.y;

    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2;

    while (true) {
        EGDrawPixel(dst, {x1, y1}, state);

        if (x1 == x2 && y1 == y2) {
            break;
        }

        int e2 = err;

        if (e2 > -dx) {
            err -= dy;
            x1 += sx;
        }

        if (e2 < dy) {
            err += dx;
            y1 += sy;
        }
    }
}

void EGDrawRect(EGGraphBuf * dst, const EGRect rect, bool filled, bool state) {
    if (filled) {
        for (int i = rect.origin.x; i < rect.origin.x + rect.size.width; i++) {
            for (int j = rect.origin.y; j < rect.origin.y + rect.size.height; j++) {
                EGDrawPixel(dst, {i, j}, state);
            }
        }
    } else {
        EGDrawLine(dst, {rect.origin.x, rect.origin.y}, {rect.origin.x + rect.size.width - 1, rect.origin.y}, state);
        EGDrawLine(dst, {rect.origin.x + rect.size.width - 1, rect.origin.y}, {rect.origin.x + rect.size.width - 1, rect.origin.y + rect.size.height - 1}, state);
        EGDrawLine(dst, {rect.origin.x + rect.size.width - 1, rect.origin.y + rect.size.height - 1}, {rect.origin.x, rect.origin.y + rect.size.height - 1}, state);
        EGDrawLine(dst, {rect.origin.x, rect.origin.y + rect.size.height - 1}, {rect.origin.x, rect.origin.y}, state);
    }
}

EGRect EGRectInset(EGRect r, int dx, int dy) {
    return {
        .origin = { r.origin.x + dx, r.origin.y + dy },
        .size = { r.size.width - 2*dx, r.size.height - 2*dy }
    };
}