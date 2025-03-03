#pragma once
#include "view.h"

namespace UI {
    /// A constant image buffer
    struct Image {
        EGBufferFormat format;
        EGSize size;
        const uint8_t * data;
    };

    class ImageView: public View {
    public:
        ImageView(const Image* i, EGRect f):
            image(i),
            View(f) {
        }

        void set_image(const Image* i) {
            image = i;
            set_needs_display();
        }

        void render(EGGraphBuf * buf) override {
            if(image != nullptr && image->data != nullptr) {
                const EGGraphBuf tmp_buf = {
                    .fmt = image->format,
                    .size = image->size,
                    .data = (EGRawGraphBuf) image->data
                };
                EGBlitBuffer(buf, EGPointZero, &tmp_buf);
            } else {
                ESP_LOGW("IVIEW", "No image or null image");
            }

            View::render(buf);
        }

    protected:
        const Image * image;
    };
}