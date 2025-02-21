#include <views/wifi_icon.h>
#include <esper-core/wlan.h>

static const uint8_t disconnect_data[] = {
    0b10001000,
    0b01010000,
    0b00100000,
    0b01010000,
    0b10001000,
};

static const uint8_t low_data[] = {
    0b00000000,
    0b00000000,
    0b00000000,
    0b00000000,
    0b10000000,
};

static const uint8_t mid_data[] = {
    0b00000000,
    0b00000000,
    0b11000000,
    0b00100000,
    0b10100000,
};

static const uint8_t hi_data[] = {
    0b11100000,
    0b00010000,
    0b11001000,
    0b00101000,
    0b10101000,
};

static const UI::Image icons[] = {
    {
        .format = EG_FMT_HORIZONTAL,
        .size = {5, 5},
        .data = disconnect_data,
    },
    {
        .format = EG_FMT_HORIZONTAL,
        .size = {5, 5},
        .data = low_data,
    },
    {
        .format = EG_FMT_HORIZONTAL,
        .size = {5, 5},
        .data = mid_data,
    },
    {
        .format = EG_FMT_HORIZONTAL,
        .size = {5, 5},
        .data = hi_data,
    },
};

WiFiIcon::WiFiIcon(EGRect f): UI::View(f) {
    _img = std::make_shared<UI::ImageView>(UI::ImageView(nullptr, {{0, 0}, f.size}));
    subviews.push_back(_img);
    cur_lvl = 0;
}

bool WiFiIcon::needs_display() {
    TickType_t now = xTaskGetTickCount();
    if(now - last_check >= pdMS_TO_TICKS(1000)) {
        int rssi = Core::Services::WLAN::rssi();
        if(rssi > 0) {
            set_level(0);
        }
        else if(rssi >= -70) {
            set_level(3);
        }
        else if(rssi >= -85) {
            set_level(2);
        }
        else if(rssi >= -90) {
            set_level(1);
        }
        else {
            set_level(0);
        }
    }
    if(cur_lvl == 0 && now - last_blink >= pdMS_TO_TICKS(500)) {
        set_image(blink_phase ? 0 : 3);
        last_blink = now;
        blink_phase = !blink_phase;
    }
    return View::needs_display();
}

void WiFiIcon::set_level(int level) {
    level = std::min(3, std::max(0, level));
    cur_lvl = level;
    set_image(level);
}

void WiFiIcon::set_image(int idx) {
    _img->set_image(&icons[idx]);
}