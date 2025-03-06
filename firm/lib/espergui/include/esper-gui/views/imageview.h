#pragma once
#include "view.h"

namespace UI {
    class ImageView: public View {
    public:
        ImageView(const EGImage* i, EGRect f):
            image(i),
            View(f) {
        }

        void set_image(const EGImage* i) {
            image = i;
            set_needs_display();
        }

        void render(EGGraphBuf * buf) override {
            EGBlitImage(buf, EGPointZero, image);
            View::render(buf);
        }

    protected:
        const EGImage * image;
    };
}