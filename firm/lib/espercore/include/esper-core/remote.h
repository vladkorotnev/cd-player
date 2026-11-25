#pragma once
#include <esp32-hal-gpio.h>
#include <driver/rmt.h>
#include <unordered_map>
#include <vector>

enum VirtualKey: uint32_t {
    RVK_START_OF_NUMBERS = 1,
    RVK_NUM_0 = RVK_START_OF_NUMBERS,
    RVK_NUM_1,
    RVK_NUM_2,
    RVK_NUM_3,
    RVK_NUM_4,
    RVK_NUM_5,
    RVK_NUM_6,
    RVK_NUM_7,
    RVK_NUM_8,
    RVK_NUM_9,
    RVK_END_OF_NUMBERS = RVK_NUM_9 + 1,
    RVK_DEL = RVK_END_OF_NUMBERS,

    RVK_DIMMER,
    RVK_SHUFFLE,
    RVK_INFO,
    RVK_LYRICS,
    RVK_DISK_NEXT,
    RVK_DISK_PREV,
    RVK_SEEK_FWD,
    RVK_SEEK_BACK,
    RVK_TRACK_PREV,
    RVK_TRACK_NEXT,
    RVK_DISP,
    RVK_PLAY,
    RVK_PAUSE,
    RVK_STOP,
    RVK_EJECT,

    RVK_MODE_SWITCH,
    RVK_MODE_CD,
    RVK_MODE_RADIO,
    RVK_MODE_BLUETOOTH,
    RVK_MODE_SETTINGS,
    RVK_MODE_STANDBY,

    RVK_CURS_UP,
    RVK_CURS_DOWN,
    RVK_CURS_LEFT,
    RVK_CURS_RIGHT,
    RVK_CURS_ENTER,

    // TODO more if needed

    RVK_MAX_INVALID = 0
};

#define RVK_IS_DIGIT(rvk) ((rvk < RVK_END_OF_NUMBERS) && (rvk >= RVK_START_OF_NUMBERS))
#define RVK_TO_DIGIT(rvk) ((int) (rvk - RVK_START_OF_NUMBERS))

namespace Core {
    namespace Remote {
        class Decoder {
        public:
            using RemoteCode = uint32_t;

            virtual ~Decoder() = default;

            /// Accept an RMT data set and change the decoder state. Returns REMDEC_GIVEN_UP if the decoding can be abandoned with this decoder. If REMDEC_GOT_CODE is returned, `code` will have the received code written into it.
            virtual bool feed(const rmt_item32_t * items, int count, RemoteCode* code) { return false; }
        protected:
            Decoder() = default;

            const int bitMargin = 300; // [us] tolerance
            bool get_bit(const rmt_item32_t &item, uint16_t high, uint16_t low) {
                return item.level0 == 0 && item.level1 != 0 && item.duration0 < (high + bitMargin) && item.duration0 > (high - bitMargin) && item.duration1 < (low + bitMargin) && item.duration1 > (low - bitMargin);
            }
        };

        class Sony20bitDecoder: public Decoder {
        public:
            explicit Sony20bitDecoder(): Decoder() {}
            bool feed(const rmt_item32_t * items, int count, RemoteCode * code) override;
        protected:
            const char * LOG_TAG = "SIRC20";
        };

        typedef std::unordered_map<Decoder::RemoteCode, VirtualKey> RemoteKeyMap; //<- someday make generic?

        class KeymapDecoder: public Decoder {
        public:
            explicit KeymapDecoder(Decoder& decoder, const std::vector<RemoteKeyMap> maps): _decoder{decoder}, _maps{maps}, Decoder() {}

            bool feed(const rmt_item32_t * items, int count, RemoteCode * code) override { 
                RemoteCode untranslated_code = 0;
                ESP_LOGD(LOG_TAG, "Code length %i", count);
                bool rslt = _decoder.feed(items, count, &untranslated_code);
                if(rslt) {
                    bool found = false;
                    for(const RemoteKeyMap& _map: _maps) {
                        auto const code_ptr = _map.find(untranslated_code);
                        if(code_ptr != _map.cend()) {
                            *code = (uint32_t) code_ptr->second;
                            ESP_LOGV(LOG_TAG, "Raw code 0x%08x -> Virtual code 0x%08x", untranslated_code, *code);
                            found = true;
                        }
                    }
                    if (!found) {
                        ESP_LOGE(LOG_TAG, "Unmapped code 0x%08x", untranslated_code);
                    }
                }  
                return rslt;
            }

        protected:
            Decoder& _decoder;
            const std::vector<RemoteKeyMap> _maps;
            const char * LOG_TAG = "KEYMAP";
        };
    }
};

namespace Platform {
    class Remote {
    public:
        using RemoteCode = Core::Remote::Decoder::RemoteCode;
        virtual ~Remote() = default;
        virtual void update() = 0;
        virtual bool has_new_code() = 0;
        virtual RemoteCode code() = 0;
    };

    class InfraredRemote: public Remote {
    public:
        explicit InfraredRemote(Core::Remote::Decoder& decoder, gpio_num_t pin);

        void update() override;

        bool has_new_code() override;
        RemoteCode code() override;
    protected:
        rmt_channel_t channel;
        RingbufHandle_t rb;
        Core::Remote::Decoder& decoder;

        bool new_code_flag = false;        
        RemoteCode last_received_code = 0;
        TickType_t last_reception_ts = 0;

        const char * LOG_TAG = "IRRC";
    };

    /// @brief Keymap for the PS2 DVD remote, which was bundled with some of the first ESPer boards
    extern Core::Remote::RemoteKeyMap PS2_REMOTE_KEYMAP;
    /// @brief Keymap for a knockoff RM-D335 remote with a custom sticker instead of the original (thus the mappings don't match the stock label on purpose)
    extern Core::Remote::RemoteKeyMap RM_D335_KEYMAP;
};
