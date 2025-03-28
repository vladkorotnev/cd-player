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
            rect.size.width = std::min(rect.size.width, framebuffer.size.width - rect.origin.x);
            rect.size.height = std::min(rect.size.height, framebuffer.size.height - rect.origin.y);
        }
        display->transfer(rects, &framebuffer);
        TickType_t blit_end = xTaskGetTickCount();

        if(pdTICKS_TO_MS(blit_end - start) > 16)
            ESP_LOGV(LOG_TAG, "Render: %i ms, Blit: %i ms, Total: %i ms, Tiles: %i", pdTICKS_TO_MS(render_end - start), pdTICKS_TO_MS(blit_end - render_end), pdTICKS_TO_MS(blit_end - start), rects.size());
    }

    /// @brief Render a view and its children into the framebuffer and create a list of rects that need blitting
    void Compositor::render_into_buffer(UI::View& view, std::vector<EGRect>* rects, const EGPoint origin, bool parent_needs_display) {
        if(view.frame.size.width <= 0 || view.frame.size.height <= 0) return;
        EGPoint abs_origin = view.frame.origin;
        abs_origin.x += origin.x;
        abs_origin.y += origin.y;

        ESP_LOGD(LOG_TAG, "Rendering %s view @ (%i, %i) [absolute (%i, %i)] size (%i, %i)", view.hidden ? "HIDDEN" : "visible", view.frame.origin.x, view.frame.origin.y, abs_origin.x, abs_origin.y, view.frame.size.width, view.frame.size.height);

        bool blit_the_dam_thing = false;

        if(view.needs_display() || parent_needs_display) {
            static EGRawGraphBuf tmp_surface = nullptr;
            static EGSize tmp_surface_geometry = framebuffer.size;
            static size_t tmp_surface_stride = tmp_surface_geometry.height/8 + ((tmp_surface_geometry.height%8) != 0);
            static size_t tmp_surface_size = tmp_surface_geometry.width * tmp_surface_stride;

            if(tmp_surface == nullptr) {
                tmp_surface = (EGRawGraphBuf) heap_caps_calloc(tmp_surface_size, 1, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
                if(tmp_surface == nullptr) {
                    ESP_LOGE(LOG_TAG, "OOM while creating temp surface of size %i", tmp_surface_size);
                    return;
                }
            }

            memset(tmp_surface, 0, tmp_surface_size);
            EGSize clampedSize = EGSize { std::min(view.frame.size.width, tmp_surface_geometry.width), std::min(view.frame.size.height, tmp_surface_geometry.height) };
            EGGraphBuf buf = {
                .fmt = framebuffer.fmt,
                .size = clampedSize,
                .data = tmp_surface
            };

            // if hidden we will blit the empty tmp_surface, effectively cleaning the area
            if(!view.hidden) view.render(&buf);
            view.clear_needs_display();

            EGBlitBuffer(&framebuffer, abs_origin, &buf);

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
            // first wipe out the hidden ones then render the non hidden ones
            for(auto subview: view.subviews) {
                if(subview->hidden)
                    render_into_buffer(*subview, blit_the_dam_thing ? nullptr : rects, abs_origin, blit_the_dam_thing);
            }
            for(auto subview: view.subviews) {
                if(!subview->hidden)
                    render_into_buffer(*subview, blit_the_dam_thing ? nullptr : rects, abs_origin, blit_the_dam_thing);
            }
        }
    }
}