#pragma once

#include <esper-cdp/atapi.h>
#include <esper-core/audio_router.h>
#include <atomic>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/timers.h>


namespace ATAPI {
    class StreamingPlayerDevice final : public HardwareATAPIDevice {
    public:
        StreamingPlayerDevice(Platform::IDEBus * bus, Platform::AudioRouter * output, BaseType_t interruptCore);
        ~StreamingPlayerDevice() override;
        bool initialize() override;

        void reset() override;
        void start(bool state = true) override;
        void eject(bool open_tray) override;
        void load_unload(SlotNumber slot) override;
        void play(MSF start, MSF end) override;
        void pause(bool pause) override;
        void stop() override;
        void scan(bool forward, MSF from) override;
        const DiscTOC read_toc() override;
        const AudioStatus * query_position() override;

    private:
        static constexpr size_t CDDA_SECTOR_BYTES = 2352;
        static constexpr unsigned SECTORS_PER_BLOCK = 16;
        static constexpr unsigned BLOCK_COUNT = 12;
        static constexpr size_t BLOCK_BYTES = SECTORS_PER_BLOCK * CDDA_SECTOR_BYTES;
        static constexpr EventBits_t PREFILLED = 1 << 0;
        static constexpr int LBA_OFFSET = 150;

        static constexpr unsigned SCAN_INTERVAL_MS = 500;
        static constexpr int SCAN_STEP_FRAMES = 2*MSF::FRAMES_IN_SECOND;

        struct Block {
            uint8_t slot;
            uint8_t sectors;
            int32_t lba;
        };

        Platform::AudioRouter * output;
        uint8_t * psram = nullptr;
        SemaphoreHandle_t control = nullptr;
        QueueHandle_t free_blocks = nullptr;
        QueueHandle_t filled_blocks = nullptr;
        EventGroupHandle_t events = nullptr;
        SemaphoreHandle_t reader_start = nullptr;
        SemaphoreHandle_t output_start = nullptr;
        SemaphoreHandle_t workers_done = nullptr;
        TimerHandle_t scan_timer = nullptr;
        std::vector<DiscTrack> tracks;
        int end_lba = 0;
        int start_lba = 0;
        int lead_out_lba = 0;
        bool session = false;
        bool playback_active = false;
        std::atomic<bool> cancelled{false};
        std::atomic<bool> shutdown{false};
        std::atomic<bool> reader_finished{false};
        std::atomic<bool> scanning{false};
        std::atomic<bool> scan_forward{true};
        std::atomic<int> scan_min_lba{-LBA_OFFSET};
        std::atomic<int> scan_max_lba{0};
        std::atomic<int> scan_target_lba{0};
        std::atomic<int> scan_position_lba{0};
        std::atomic<unsigned> pcm_bytes{0};
        std::atomic<AudioStatus::PlayState> state{AudioStatus::PlayState::Stopped};
        AudioStatus position = {};
        BaseType_t interruptCore = tskNO_AFFINITY;

        void play_locked(int first, int end);
        bool begin_playback_locked(int first, int end);
        bool start_workers_locked();
        void cancel_playback_locked();
        void stop_locked();
        int position_lba() const;
        void read_blocks();
        /// Read CD-DA main-channel PCM (little-endian stereo) starting at an LBA.
        bool read_cd_audio(uint32_t lba, uint16_t sectors, uint8_t * pcm);
        void output_blocks();
        static void scan_timer_callback(TimerHandle_t timer);
        static void reader_task(void * context);
        static void output_task(void * context);
    };
}
