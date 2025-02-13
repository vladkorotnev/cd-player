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
    void Compositor::render(UI::View& view) {
        std::vector<EGRect> rects = {};
        TickType_t start = xTaskGetTickCount();
        render_into_buffer(view, &rects, {0, 0}, false);
        if(rects.empty()) return;
        TickType_t render_end = xTaskGetTickCount();
        for(auto& rect: rects) {
            display->transfer(rect.origin, rect.size, &framebuffer);
        }
        TickType_t blit_end = xTaskGetTickCount();
        ESP_LOGD(LOG_TAG, "Render: %i ms, Blit: %i ms, Total: %i ms, Tiles: %i", pdTICKS_TO_MS(render_end - start), pdTICKS_TO_MS(blit_end - render_end), pdTICKS_TO_MS(blit_end - start), rects.size());
    }

    /// @brief Render a view and its children into the framebuffer and create a list of rects that need blitting
    void Compositor::render_into_buffer(UI::View& view, std::vector<EGRect>* rects, const EGPoint origin, bool parent_needs_display) {
        if(view.frame.size.width <= 0 || view.frame.size.height <= 0) return;
        EGPoint abs_origin = view.frame.origin;
        abs_origin.x += origin.x;
        abs_origin.y += origin.y;

        size_t stride = framebuffer.size.height/8;
        size_t view_stride = std::max(view.frame.size.height/8, 1u);

        ESP_LOGD(LOG_TAG, "Rendering %s view @ (%i, %i) [absolute (%i, %i)] size (%i, %i)", view.hidden ? "HIDDEN" : "visible", view.frame.origin.x, view.frame.origin.y, abs_origin.x, abs_origin.y, view.frame.size.width, view.frame.size.height);

        bool blit_the_dam_thing = false;
        bool needed_display = false;

        if(view.needs_display() || parent_needs_display) {
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
            else needed_display = true;
            view.clear_needs_display();

            if(tmp_surface != framebuffer.data) {
                uint8_t row_first = std::max(0, abs_origin.y) / 8;
                uint8_t row_last = (abs_origin.y + view.frame.size.height - 1) / 8;
                uint8_t bit_offs = std::max(0, abs_origin.y) % 8;

                for(int col = 0; col < std::min(view.frame.size.width, (framebuffer.size.width - view.frame.origin.x)); col++) {
                    for(int row = 0; row < stride; row++) {
                        if(row >= row_first && row <= row_last) {
                            uint8_t dst_byte = 0;
                            uint8_t dst_mask = 0;
                            size_t surf_idx = ((col + std::max(0, -abs_origin.x)) * view_stride) + (row - row_first) + std::max(0, -abs_origin.y);
                            if(row > row_first && bit_offs > 0) {
                                dst_byte |= (tmp_surface[surf_idx - 1] << (8 - bit_offs));
                                dst_mask |= (0xFF << (8 - bit_offs));
                            }
                            dst_byte |= (tmp_surface[surf_idx] >> bit_offs);
                            dst_mask |= (0xFF >> bit_offs);
                            int px_remain = view.frame.size.height - (row - row_first)*(8 - bit_offs) - (row > row_first ? bit_offs : 0);
                            if(px_remain < 8 - bit_offs) {
                                // we have less pixels remaining than we can fit in this byte, trim mask accordingly to not clip whatever we have in the same byte
                                dst_mask &= 0xFF << (8 - (px_remain + bit_offs));
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
            if(rects != nullptr) {
                EGRect rslt = { 0 };
                rslt.origin = abs_origin;
                rslt.size = view.frame.size;
                rects->push_back(rslt);
            }
            blit_the_dam_thing = true;
        }

        if(!view.hidden) {
            for(auto subview: view.subviews) {
                render_into_buffer(*subview, blit_the_dam_thing ? nullptr : rects, abs_origin, needed_display);
            }
        }
    }
}