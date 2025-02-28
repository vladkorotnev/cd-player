#include <esper-core/remote.h>

static char LOG_TAG[] = "IRRC";

namespace Platform {
    Remote::Remote(gpio_num_t pin):
        receiver(pin) {
            ESP_LOGI(LOG_TAG, "started");
    }

    void Remote::update() {
        receiver.task();

        if(receiver.sony_available()) {
            ESP_LOGI(LOG_TAG, "code = 0x%08x", receiver.data());
        }
    }
}