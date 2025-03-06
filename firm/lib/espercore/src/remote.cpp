#include <esper-core/remote.h>

// Seems like there is not yet any library that can use the RMT to receive instead of a dumb interrupt and/or timer
// That won't cut it in our pretty tight scenarios (e.g. when decoding internet radio -- CPU time is precious!)
// I'm too burnt out and tired to roll a library, but I can roll a single use thing here at least!

namespace Platform {
    Remote::Remote(Core::Remote::Decoder& dec, gpio_num_t pin): channel{RMT_CHANNEL_1}, decoder{dec}, rb{NULL} {
        rmt_config_t rmt_rx_config = RMT_DEFAULT_CONFIG_RX(pin, channel);
        ESP_ERROR_CHECK(rmt_config(&rmt_rx_config));
        ESP_ERROR_CHECK(rmt_driver_install(channel, 1000, 0));
        ESP_ERROR_CHECK(rmt_get_ringbuf_handle(channel, &rb));
        ESP_ERROR_CHECK(rmt_rx_start(channel, true));
        assert(rb != NULL);
    }

    void Remote::update() {
        rmt_item32_t *items = NULL;
        size_t length = 0;

        items = (rmt_item32_t *) xRingbufferReceive(rb, &length, pdMS_TO_TICKS(33));
        if (items) {
            length /= sizeof(rmt_item32_t); 
            RemoteCode code = 0;
            TickType_t now = xTaskGetTickCount();

            if(decoder.feed(items, length, &code)) {
                ESP_LOGV(LOG_TAG, "Got IRRC Code: 0x%08x", code);
                
                if(last_received_code != code || now - last_reception_ts > pdMS_TO_TICKS(100)) {
                    new_code_flag = true;
                }

                last_received_code = code;
                last_reception_ts = now;
            }

            vRingbufferReturnItem(rb, (void *) items);
        }
    }

    bool Remote::has_new_code() {
        TickType_t now = xTaskGetTickCount();
        bool rslt = (now - last_reception_ts <= pdMS_TO_TICKS(100) && new_code_flag);
        new_code_flag = false;
        return rslt;
    }

    Platform::Remote::RemoteCode Remote::code() {
        return last_received_code;
    }
}

namespace Core {
    namespace Remote {
        bool Sony20bitDecoder::feed(const rmt_item32_t * items, int count, RemoteCode * outCode) {
            const uint8_t totalMin = 12;
            uint8_t i = 0;
            if(count < totalMin || !get_bit(items[0], 2400, 600)){
                ESP_LOGD(LOG_TAG, "No header");
                return 0;
            }
            i++;
            uint32_t code = 0;
            uint8_t maxData = 20;
            if(count < maxData){maxData = 15;}
            if(count < maxData){maxData = 12;}
            for(int j = 0; j < maxData - 1; j++){
                if(get_bit(items[i], 1200, 600)){
                    code |= (1 << j);
                }else if(get_bit(items[i], 600, 600)){
                    code |= (0 << j); // technically no-op
                }else if(items[i].duration1 > 15000){ //space repeats
                    break;
                }else{
                    return false;
                }
                i++;
            }
            *outCode = code;
            return true;
        }
    }
}

Core::Remote::RemoteKeyMap Platform::PS2_REMOTE_KEYMAP = {
    // PS2  ------------ Virtual Key
    {0x49D09,        RVK_NUM_0},
    {0x49D00,        RVK_NUM_1},
    {0x49D01,        RVK_NUM_2},
    {0x49D02,        RVK_NUM_3},
    {0x49D03,        RVK_NUM_4},
    {0x49D04,        RVK_NUM_5},
    {0x49D05,        RVK_NUM_6},
    {0x49D06,        RVK_NUM_7},
    {0x49D07,        RVK_NUM_8},
    {0x49D08,        RVK_NUM_9},
    {0x49D0F,        RVK_DEL}, // "Clear"
    {0x49D28,        RVK_DISP}, // "Time"

    {0x49D32,       RVK_PLAY},
    {0x49D39,       RVK_PAUSE},
    {0x49D38,       RVK_STOP},

    {0x49D64,       RVK_DIMMER}, // "Audio"
    {0x49D65,       RVK_INFO},   // "Angle"
    {0x49D63,       RVK_LYRICS}, // "Lyrics"
    {0x49D35,       RVK_SHUFFLE}, // "Shuffle"
    {0x49D1F,       RVK_PROGRAM}, // "Program"
    {0x49D2C,       RVK_REPEAT}, // "Repeat"

    {0x49D60,       RVK_DISK_PREV}, // "Slow <<"
    {0x49D61,       RVK_DISK_NEXT}, // "Slow >>"
    {0x49D33,       RVK_SEEK_BACK}, // "Scan <<"
    {0x49D34,       RVK_SEEK_FWD},  // "Scan >>"
    {0x49D30,       RVK_TRACK_PREV}, // "Prev"
    {0x49D31,       RVK_TRACK_NEXT}, // "Next"
    {0x49D2A,       RVK_AB_REPEAT}, // "A-B"

    {0x49D54,       RVK_MODE_CD}, // "Display"
    {0x49D1A,       RVK_MODE_RADIO}, // "Title"
    {0x49D1B,       RVK_MODE_BLUETOOTH}, // "DVD Menu"
    {0x49D0E,       RVK_MODE_SETTINGS},

    {0x49D79,       RVK_CURS_UP},
    {0x49D7A,       RVK_CURS_DOWN},
    {0x49D7B,       RVK_CURS_LEFT},
    {0x49D7C,       RVK_CURS_RIGHT},
    {0x49D0B,       RVK_CURS_ENTER},

    // below are just for their shapes, not really used, but to keep the codes handy...
    {0x5AD5C,       RVK_EJECT}, // Triangle
    {0x5AD5F,       RVK_STOP},  // Square
    {0x5AD5D,       RVK_MODE_SWITCH}, // Circle
    {0x5AD5E,       RVK_MODE_STANDBY}, // Cross

    {0x5AD50,       RVK_STOP}, // Select
    {0x5AD53,       RVK_PLAY}, // Start
    // 0x5AD5A: L1
    // 0x5AD58: L2
    // 0x5AD51: L3
    // 0x5AD5B: R1
    // 0x5AD59: R2
    // 0x5AD52: R3
};