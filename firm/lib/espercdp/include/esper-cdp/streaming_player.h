#pragma once

#include <esper-cdp/atapi.h>
#include <esper-core/audio_router.h>
#include <atomic>
#include <freertos/event_groups.h>
#include <freertos/queue.h>


namespace ATAPI {
    class StreamingPlayerDevice final : public HardwareATAPIDevice {
    public:
        StreamingPlayerDevice(Platform::IDEBus * bus, Platform::AudioRouter * output, BaseType_t interruptCore);
        ~StreamingPlayerDevice() override;
        bool initialize();

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

        struct Block {
            uint8_t slot;
            uint8_t sectors;
        };

        Platform::AudioRouter * output;
        uint8_t * psram = nullptr;
        SemaphoreHandle_t control = nullptr;
        QueueHandle_t free_blocks = nullptr;
        QueueHandle_t filled_blocks = nullptr;
        EventGroupHandle_t events = nullptr;
        SemaphoreHandle_t workers_done = nullptr;
        std::vector<DiscTrack> tracks;
        int end_lba = 0;
        int start_lba = 0;
        bool session = false;
        std::atomic<bool> cancelled{false};
        std::atomic<bool> reader_finished{false};
        std::atomic<unsigned> pcm_bytes{0};
        std::atomic<AudioStatus::PlayState> state{AudioStatus::PlayState::Stopped};
        AudioStatus position = {};
        BaseType_t interruptCore = tskNO_AFFINITY;

        void play_locked(int first, int end);
        void stop_locked();
        int position_lba() const;
        void read_blocks();
        /// Read CD-DA main-channel PCM (little-endian stereo) starting at an LBA.
        bool read_cd_audio(uint32_t lba, uint16_t sectors, uint8_t * pcm);
        void output_blocks();
        static void reader_task(void * context);
        static void output_task(void * context);
    };
}
