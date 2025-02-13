#include <esper-gui/compositing.h>
#include <esp32-hal-log.h>
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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

namespace Graphics {
    void Compositor::render(View& view) {
        std::vector<EGRect> rects = {};
        TickType_t start = xTaskGetTickCount();
        render_into_buffer(view, &rects, {0, 0});
        if(rects.empty()) return;
        TickType_t render_end = xTaskGetTickCount();
        for(auto& rect: rects) {
            display->blit(rect.origin, rect.size, &framebuffer);
        }
        TickType_t blit_end = xTaskGetTickCount();
        ESP_LOGV(LOG_TAG, "Render: %i ms, Blit: %i ms, Total: %i ms, Tiles: %i", pdTICKS_TO_MS(render_end - start), pdTICKS_TO_MS(blit_end - render_end), pdTICKS_TO_MS(blit_end - start), rects.size());
    }

    /// @brief Convert view frame to parameter for display driver's blit call
    EGRect Compositor::absolute_rect_to_tile_grid(const EGRect frame) {
        EGRect rslt = { 0 };
        rslt.origin.x = std::max((uint16_t)0, frame.origin.x);
        rslt.origin.y = std::max((uint8_t)0, frame.origin.y);

        // account for negative X and Y
        rslt.size.width = frame.size.width + std::min((uint16_t)0, frame.origin.x); 
        rslt.size.height = frame.size.height + std::min((uint8_t)0, frame.origin.y);

        // clamp to display size
        rslt.size.width = std::min(framebuffer.size.width, rslt.size.width);
        rslt.size.height = std::min(framebuffer.size.height, rslt.size.height);

        // TBD: Need alignment to byte? For now expect display driver to do that
        return rslt;
    }

    /// @brief Render a view and its children into the framebuffer and create a list of rects that need blitting
    void Compositor::render_into_buffer(View& view, std::vector<EGRect>* rects, const EGPoint origin) {
        if(view.frame.size.width <= 0 || view.frame.size.height <= 0) return;
        EGPoint abs_origin = view.frame.origin;
        abs_origin.x += origin.x;
        abs_origin.y += origin.y;

        size_t stride = framebuffer.size.height/8;
        size_t view_stride = std::max(view.frame.size.height/8, 1);

        ESP_LOGD(LOG_TAG, "Rendering %s view @ (%i, %i) [absolute (%i, %i)] size (%i, %i)", view.hidden ? "HIDDEN" : "visible", view.frame.origin.x, view.frame.origin.y, abs_origin.x, abs_origin.y, view.frame.size.width, view.frame.size.height);

        bool blit_the_dam_thing = false;

        if(view.needs_display()) {
            size_t surface_size = view.frame.size.width * view_stride;
            
            GraphBuf tmp_surface = nullptr;
            if(!EGRectEqual(view.frame, {{0, 0}, framebuffer.size})) {
                tmp_surface = (GraphBuf) calloc(surface_size, 1);
            } else {
                // save memory by rendering fullscreen views directly into the framebuffer
                tmp_surface = framebuffer.data;
                memset(tmp_surface, 0, surface_size);
            }

            if(tmp_surface == nullptr) {
                ESP_LOGE(LOG_TAG, "OOM while creating temp surface of size (%i, %i) = %i", view.frame.size.width, view.frame.size.height, surface_size);
                return;
            }

            // if hidden we will blit the empty tmp_surface, effectively cleaning the area
            if(!view.hidden) view.render(tmp_surface);
            view.clear_needs_display();

            if(tmp_surface != framebuffer.data) {
                uint8_t row_first = abs_origin.y / 8;
                uint8_t row_last = (abs_origin.y + view.frame.size.height - 1) / 8;
                uint8_t bit_offs = abs_origin.y % 8;

                for(int col = 0; col < std::min(view.frame.size.width, (uint16_t) (framebuffer.size.width - view.frame.origin.x)); col++) {
                    for(int row = 0; row < stride; row++) {
                        if(row >= row_first && row <= row_last) {
                            uint8_t dst_byte = 0;
                            uint8_t dst_mask = 0;
                            size_t surf_idx = (col * view_stride) + (row - row_first);
                            if(row > row_first && bit_offs > 0) {
                                dst_byte |= (tmp_surface[surf_idx - 1] << (8 - bit_offs));
                                dst_mask |= (0xFF << (8 - bit_offs));
                            }
                            dst_byte |= (tmp_surface[surf_idx] >> bit_offs);
                            dst_mask |= (0xFF >> bit_offs);
                            int px_remain = view.frame.size.height - (row - row_first)*(8 - bit_offs) - (row > row_first ? bit_offs : 0);
                            if(px_remain < 8 - bit_offs) {
                                // we have less pixels remaining than we can fit in this byte, trim mask accordingly to not clip whatever we have in the same byte
                                uint8_t m = 0;
                                for(int i = 0; i < px_remain + bit_offs; i++) {
                                    m >>= 1;
                                    m |= 0x80;
                                }
                                dst_mask &= m;
                            }

                            size_t fb_idx = (abs_origin.x + col) * stride + row;
                            framebuffer.data[fb_idx] &= ~dst_mask;
                            framebuffer.data[fb_idx] |= (dst_byte & dst_mask);
                        }
                    }
                }
            
               free(tmp_surface);
            }

            // The actual view itself is dirty, so it needs blitting as a whole
            if(rects != nullptr)
                rects->push_back(absolute_rect_to_tile_grid(view.frame));
            blit_the_dam_thing = true;
        }

        if(!view.hidden) {
            for(auto subview: view.subviews) {
                render_into_buffer(*subview, blit_the_dam_thing ? nullptr : rects, abs_origin);
            }
        }
    }
}