#pragma once
#include "view.h"

namespace UI {
    enum ImageFormat {
        IMFMT_NATIVE,
    };

    struct Image {
        ImageFormat format;
        EGSize size;
        const uint8_t * data;
    };

    class ImageView: public View {
    public:
        ImageView(Image* i, EGRect f):
            image(i),
            View(f) {
        }

        void set_image(Image* i) {
            image = i;
            set_needs_display();
        }

        void render(GraphBuf buf) override {
            if(image != nullptr && image->data != nullptr) {
                if(image->format == IMFMT_NATIVE) {
                    size_t buf_stride = frame.size.height / 8;
                    size_t img_stride = image->size.height / 8;
                    for(int i = 0; i < frame.size.width; i++) {
                        for(int j = 0; j < std::min(buf_stride, img_stride); j++) {
                            buf[i*buf_stride + j] = image->data[i*img_stride + j];
                        }
                    }
                }
            }
        }

    protected:
        Image * image = nullptr;
    };
}