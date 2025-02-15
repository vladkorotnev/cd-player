#include <esper-gui/compositing.h>
#include <esp32-hal-log.h>
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char LOG_TAG[] = "EGC";

namespace Graphics {
    void Compositor::render(UI::View& view) {
        std::vector<EGRect> rects = {};
        TickType_t start = xTaskGetTickCount();
        render_into_buffer(view, &rects, {0, 0}, false);
        if(rects.empty()) return;
        TickType_t render_end = xTaskGetTickCount();
        for(auto& rect: rects) {
            // clamp rect to bounds of buffer, since screen driver cannot process negative coords
            rect.size.width -= std::max(0, -rect.origin.x);
            rect.origin.x = std::max(0, rect.origin.x);
            rect.size.height -= std::max(0, -rect.origin.y);
            rect.origin.y = std::max(0, rect.origin.y);

            display->transfer(rect.origin, rect.size, &framebuffer);
        }
        TickType_t blit_end = xTaskGetTickCount();

        if(pdTICKS_TO_MS(blit_end - start) > 1)
            ESP_LOGI(LOG_TAG, "Render: %i ms, Blit: %i ms, Total: %i ms, Tiles: %i", pdTICKS_TO_MS(render_end - start), pdTICKS_TO_MS(blit_end - render_end), pdTICKS_TO_MS(blit_end - start), rects.size());
    }

    /// @brief Render a view and its children into the framebuffer and create a list of rects that need blitting
    void Compositor::render_into_buffer(UI::View& view, std::vector<EGRect>* rects, const EGPoint origin, bool parent_needs_display) {
        if(view.frame.size.width <= 0 || view.frame.size.height <= 0) return;
        EGPoint abs_origin = view.frame.origin;
        abs_origin.x += origin.x;
        abs_origin.y += origin.y;

        ESP_LOGD(LOG_TAG, "Rendering %s view @ (%i, %i) [absolute (%i, %i)] size (%i, %i)", view.hidden ? "HIDDEN" : "visible", view.frame.origin.x, view.frame.origin.y, abs_origin.x, abs_origin.y, view.frame.size.width, view.frame.size.height);

        bool blit_the_dam_thing = false;
        bool needed_display = false;

        if(view.needs_display() || parent_needs_display) {
            size_t view_stride = std::max(view.frame.size.height/8, 1u);
            size_t surface_size = view.frame.size.width * view_stride;
            
            EGRawGraphBuf tmp_surface = nullptr;
            if(!EGRectEqual(view.frame, {EGPointZero, framebuffer.size})) {
                tmp_surface = (EGRawGraphBuf) calloc(surface_size, 1);
            } else {
                // save memory by rendering fullscreen views directly into the framebuffer
                tmp_surface = framebuffer.data;
                memset(tmp_surface, 0, surface_size);
            }

            if(tmp_surface == nullptr) {
                ESP_LOGE(LOG_TAG, "OOM while creating temp surface of size (%i, %i) = %i", view.frame.size.width, view.frame.size.height, surface_size);
                return;
            }

            EGGraphBuf buf = {
                .fmt = framebuffer.fmt,
                .size = view.frame.size,
                .data = tmp_surface
            };

            // if hidden we will blit the empty tmp_surface, effectively cleaning the area
            if(!view.hidden) view.render(&buf);
            needed_display = true;
            view.clear_needs_display();

            if(tmp_surface != framebuffer.data) {
                EGBlitBuffer(&framebuffer, abs_origin, &buf);
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