#include <esper-cdp/streaming_device.h>
#include <esper-cdp/atapi-protocol.h>
#include <esper-core/audio_router.h>
#include <esper-core/lock.h>
#include <algorithm>
#include <esp_heap_caps.h>
#include <esp_timer.h>

namespace {
    constexpr char LOG_TAG[] = "CD_STREAM";
    constexpr TickType_t BUFFER_WAIT = pdMS_TO_TICKS(20);
    constexpr TickType_t OUTPUT_WAIT = pdMS_TO_TICKS(1);
    constexpr unsigned PREFILL_BLOCKS = 2;
    constexpr unsigned LOG_INTERVAL_SECTORS = 512;
    constexpr unsigned PCM_BYTES_PER_SECOND = 44100 * 2 * sizeof(int16_t);
    constexpr unsigned I2S_BUFFER_BYTES = ESPER_AUDIO_BUFFER_SIZE * ESPER_AUDIO_BUFFER_COUNT * 4;
}

namespace ATAPI {
    StreamingPlayerDevice::StreamingPlayerDevice(Platform::IDEBus * bus_, Platform::AudioRouter * output, BaseType_t core):
        HardwareATAPIDevice(bus_), output(output), interruptCore(core) {}

    bool StreamingPlayerDevice::initialize() {
        if (!HardwareATAPIDevice::initialize()) return false;
        quirks.must_use_softscan = false; //<- software playback never uses softscan since technically it's always softscan

        control = xSemaphoreCreateMutex();
        workers_done = xSemaphoreCreateCounting(2, 0);
        events = xEventGroupCreate();
        free_blocks = xQueueCreate(BLOCK_COUNT, sizeof(uint8_t));
        filled_blocks = xQueueCreate(BLOCK_COUNT, sizeof(Block));
        psram = static_cast<uint8_t *>(heap_caps_malloc(BLOCK_COUNT * BLOCK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!control || !workers_done || !events || !free_blocks || !filled_blocks || !psram) {
            ESP_LOGE(LOG_TAG, "Could not allocate CD audio stream");
            return false;
        }
        ESP_LOGI(LOG_TAG, "%u sectors per READ CD, %zu bytes of PCM buffer in PSRAM, target %u B/s",
                 SECTORS_PER_BLOCK, BLOCK_COUNT * BLOCK_BYTES, PCM_BYTES_PER_SECOND);
        return true;
    }

    StreamingPlayerDevice::~StreamingPlayerDevice() {
        if (session) stop_locked();
        if (workers_done) vSemaphoreDelete(workers_done);
        if (events) vEventGroupDelete(events);
        if (free_blocks) vQueueDelete(free_blocks);
        if (filled_blocks) vQueueDelete(filled_blocks);
        if (control) vSemaphoreDelete(control);
        heap_caps_free(psram);
    }

    void StreamingPlayerDevice::reset() {
        Lock lock(control);
        stop_locked();
        tracks.clear();
        HardwareATAPIDevice::reset();
    }

    void StreamingPlayerDevice::start(bool state) {
        Lock lock(control);
        if (!state) stop_locked();
        HardwareATAPIDevice::start(state);
    }

    void StreamingPlayerDevice::eject(bool open_tray) {
        Lock lock(control);
        stop_locked();
        tracks.clear();
        HardwareATAPIDevice::eject(open_tray);
    }

    void StreamingPlayerDevice::load_unload(SlotNumber slot) {
        Lock lock(control);
        stop_locked();
        tracks.clear();
        HardwareATAPIDevice::load_unload(slot);
    }

    const DiscTOC StreamingPlayerDevice::read_toc() {
        Lock lock(control);
        stop_locked();
        auto toc = HardwareATAPIDevice::read_toc();
        tracks = toc.tracks;
        return toc;
    }

    void StreamingPlayerDevice::play(MSF start, MSF end) {
        Lock lock(control);
        int first = std::max(0, MSF_TO_FRAMES(start) - LBA_OFFSET);
        int limit = MSF_TO_FRAMES(end) - LBA_OFFSET;
        for (auto track = tracks.rbegin(); track != tracks.rend(); ++track) {
            const int track_start = MSF_TO_FRAMES(track->position) - LBA_OFFSET;
            if (track_start <= first) {
                if (track->is_data) limit = first;
                break;
            }
            if (track->is_data) limit = std::min(limit, track_start);
        }
        play_locked(first, limit);
    }

    void StreamingPlayerDevice::play_locked(int first, int end) {
        stop_locked();
        if (first >= end) return;
        auto * sink = output->get_io_port_nub();
        if (!sink || !static_cast<bool>(*sink)) {
            ESP_LOGE(LOG_TAG, "I2S output is inactive");
            return;
        }

        start_lba = first;
        end_lba = end;
        cancelled = false;
        reader_finished = false;
        pcm_bytes = 0;
        xQueueReset(free_blocks);
        xQueueReset(filled_blocks);
        xEventGroupClearBits(events, PREFILLED);
        for (uint8_t slot = 0; slot < BLOCK_COUNT; ++slot) xQueueSend(free_blocks, &slot, 0);

        state = AudioStatus::PlayState::Playing;
        session = true;
        if (xTaskCreate(output_task, "CD-I2S", 4096, this, 6, nullptr) != pdPASS) {
            session = false;
            state = AudioStatus::PlayState::Stopped;
            ESP_LOGE(LOG_TAG, "Could not start I2S output task");
        } else if (xTaskCreatePinnedToCore(reader_task, "CD-Read", 4096, this, 5, nullptr, interruptCore) != pdPASS) {
            ESP_LOGE(LOG_TAG, "Could not start CD reader task");
            xSemaphoreGive(workers_done);
            stop_locked();
        }
    }

    void StreamingPlayerDevice::stop_locked() {
        if (session) {
            cancelled = true;
            xEventGroupSetBits(events, PREFILLED);
            xSemaphoreTake(workers_done, portMAX_DELAY);
            xSemaphoreTake(workers_done, portMAX_DELAY);
            session = false;
            output->clear_output();
        }
        state = AudioStatus::PlayState::Stopped;
    }

    void StreamingPlayerDevice::stop() {
        Lock lock(control);
        stop_locked();
    }

    void StreamingPlayerDevice::pause(bool pause) {
        Lock lock(control);
        if (pause && state == AudioStatus::PlayState::Playing) {
            const int resume_at = position_lba();
            stop_locked();
            start_lba = resume_at;
            pcm_bytes = 0;
            state = AudioStatus::PlayState::Paused;
        } else if (!pause && state == AudioStatus::PlayState::Paused) {
            play_locked(start_lba, end_lba);
        }
    }

    void StreamingPlayerDevice::scan(bool forward, MSF from) {
        Lock lock(control);
        const int lba = MSF_TO_FRAMES(from) - LBA_OFFSET;
        const int step = 5 * MSF::FRAMES_IN_SECOND;
        play_locked(std::max(0, lba + (forward ? step : -step)), end_lba);
    }

    int StreamingPlayerDevice::position_lba() const {
        // Estimate the audible position from bytes queued to I2S, allowing
        // for its DMA ring. The drive's read head can be much farther ahead.
        const unsigned queued = pcm_bytes.load();
        const unsigned played = queued - std::min(queued, I2S_BUFFER_BYTES);
        return start_lba + played / CDDA_SECTOR_BYTES;
    }

    const AudioStatus * StreamingPlayerDevice::query_position() {
        Lock lock(control);
        const auto playback = state.load();
        const int frame = position_lba() + LBA_OFFSET;
        position = {};
        position.state = playback;
        position.index = 1;
        position.position_in_disc = FRAMES_TO_MSF(frame);
        for (const auto& track : tracks) {
            const int track_start = MSF_TO_FRAMES(track.position);
            if (track_start > frame) break;
            position.track = track.number;
            position.position_in_track = FRAMES_TO_MSF(frame - track_start);
        }
        return &position;
    }

    void StreamingPlayerDevice::reader_task(void * context) {
        auto& stream = *static_cast<StreamingPlayerDevice *>(context);
        stream.read_blocks();
        stream.reader_finished = true;
        xEventGroupSetBits(stream.events, PREFILLED);
        xSemaphoreGive(stream.workers_done);
        vTaskDelete(nullptr);
    }

    void StreamingPlayerDevice::output_task(void * context) {
        auto& stream = *static_cast<StreamingPlayerDevice *>(context);
        stream.output_blocks();
        xSemaphoreGive(stream.workers_done);
        vTaskDelete(nullptr);
    }

    bool StreamingPlayerDevice::read_cd_audio(uint32_t lba, uint16_t sectors, uint8_t * pcm) {
        if (!pcm || sectors == 0) return false;
        const Requests::ReadCd request = {
            .opcode = OperationCodes::READ_CD,
            .sector_type = Requests::ReadCd::SECT_TYPE_CDDA,
            .lba = {uint8_t(lba >> 24), uint8_t(lba >> 16), uint8_t(lba >> 8), uint8_t(lba)},
            .transfer_length_blocks = {0, uint8_t(sectors >> 8), uint8_t(sectors)},
            .user = true,
        };
        static_assert(sizeof(request) == 12);
        static_assert(CDDA_SECTOR_BYTES <= Platform::SPIBridgedIDEBus::MAX_READ_BYTES);
        static_assert(Platform::SPIBridgedIDEBus::MAX_READ_BYTES <= 0xFFFE);
        const size_t total = size_t(sectors) * CDDA_SECTOR_BYTES;
        constexpr size_t max_phase = Platform::SPIBridgedIDEBus::MAX_READ_BYTES /
                                     CDDA_SECTOR_BYTES * CDDA_SECTOR_BYTES;
        const auto phase_limit = static_cast<uint16_t>(std::min(total, max_phase));

        xSemaphoreTake(semaphore, portMAX_DELAY);
        const auto before = read_sts_regi();
        if (before.BSY || before.DRQ) {
            ESP_LOGE(LOG_TAG, "Drive busy before READ CD: status=0x%02x", before.value);
            xSemaphoreGive(semaphore);
            return false;
        }
        // Allow several sectors per PIO phase when they fit in one SPI DMA burst.
        // The drive can return smaller phases, so use its reported byte count.
        ide->write(IDE::Feature, {{0, 0xFF}});
        ide->write(IDE::CylinderLow, {{uint8_t(phase_limit), 0xFF}});
        ide->write(IDE::CylinderHigh, {{uint8_t(phase_limit >> 8), 0xFF}});
        bool complete = false;
        size_t received = 0;
        if (write_packet(&request, sizeof(request), true)) {
            const TickType_t started = xTaskGetTickCount();
            do {
                const auto status = read_sts_regi();
                if (status.BSY) {
                    delay(1);
                    continue;
                }
                if (status.ERR || status.DF) break;

                // Sector Count is Interrupt Reason during a PACKET command.
                const uint8_t reason = ide->read(IDE::SectorCount).low & 0x03;
                if (!status.DRQ) {
                    if (reason == 0x03) {
                        complete = received == total;
                        break;
                    }
                    delay(1);
                    continue;
                }

                const uint8_t low = ide->read(IDE::CylinderLow).low;
                const uint8_t high = ide->read(IDE::CylinderHigh).low;
                const size_t count = (size_t(high) << 8) | low;
                if (reason != 0x02 || !count || (count & 1) ||
                    count > phase_limit || count > total - received ||
                    !ide->read_data(pcm + received, count / 2)) {
                    ESP_LOGE(LOG_TAG, "READ CD data phase: reason=0x%x, bytes=%zu", reason, count);
                    break;
                }
                received += count;
                delayMicroseconds(1);
            } while (xTaskGetTickCount() - started < pdMS_TO_TICKS(5000));
        }

        if (!complete) {
            auto status = read_sts_regi();
            ESP_LOGE(LOG_TAG, "READ CD failed at LBA %lu: %zu/%zu bytes, status=0x%02x",
                     static_cast<unsigned long>(lba), received, total, status.value);
            if (status.BSY || status.DRQ) {
                ide->reset();
                const TickType_t started = xTaskGetTickCount();
                do {
                    status = read_sts_regi();
                    if (!status.BSY && !status.DRQ) break;
                    delay(10);
                } while (xTaskGetTickCount() - started < pdMS_TO_TICKS(5000));
            }
        }
        // Restore the byte count limit expected by the existing packet commands
        // once the task file is writable.
        const auto status = read_sts_regi();
        if (!status.BSY && !status.DRQ) {
            ide->write(IDE::CylinderLow, {{0x00, 0xFF}});
            ide->write(IDE::CylinderHigh, {{0x02, 0xFF}});
        }
        xSemaphoreGive(semaphore);
        return complete;
    }

    void StreamingPlayerDevice::read_blocks() {
        int lba = start_lba;
        unsigned blocks = 0;
        unsigned bytes = 0;
        unsigned next_log_sector = LOG_INTERVAL_SECTORS;
        int64_t read_us = 0;
        while (!cancelled && lba < end_lba) {
            Block block = {};
            if (xQueueReceive(free_blocks, &block.slot, BUFFER_WAIT) != pdTRUE) continue;
            if (cancelled) break;

            block.sectors = std::min<int>(SECTORS_PER_BLOCK, end_lba - lba);
            const int64_t before = esp_timer_get_time();
            if (!read_cd_audio(lba, block.sectors, psram + block.slot * BLOCK_BYTES)) break;
            read_us += esp_timer_get_time() - before;
            if (cancelled) break;
            lba += block.sectors;
            bytes += block.sectors * CDDA_SECTOR_BYTES;
            xQueueSend(filled_blocks, &block, 0);
            if (++blocks == PREFILL_BLOCKS) xEventGroupSetBits(events, PREFILLED);
            const unsigned sectors_read = lba - start_lba;
            if (sectors_read >= next_log_sector && read_us > 0) {
                next_log_sector += LOG_INTERVAL_SECTORS;
                ESP_LOGI(LOG_TAG, "READ CD average %u B/s, queued %u blocks",
                         unsigned(uint64_t(bytes) * 1000000 / read_us), uxQueueMessagesWaiting(filled_blocks));
            }
        }
        if (read_us > 0) {
            ESP_LOGI(LOG_TAG, "Read %u PCM bytes at %u B/s (drive and SPI, excluding queue waits)",
                     bytes, unsigned(uint64_t(bytes) * 1000000 / read_us));
        }
    }

    void StreamingPlayerDevice::output_blocks() {
        xEventGroupWaitBits(events, PREFILLED, pdFALSE, pdTRUE, portMAX_DELAY);
        auto& sink = *output->get_io_port_nub();
        static const uint8_t silence[CDDA_SECTOR_BYTES] = {};
        unsigned underruns = 0;
        bool starving = false;
        while (!cancelled) {
            Block block;
            if (xQueueReceive(filled_blocks, &block, OUTPUT_WAIT) != pdTRUE) {
                if (reader_finished) {
                    if (uxQueueMessagesWaiting(filled_blocks) != 0) continue;
                    // Let the last queued samples reach the DAC.
                    const unsigned drain_ms = I2S_BUFFER_BYTES * 1000 / PCM_BYTES_PER_SECOND + 1;
                    for (unsigned ms = 0; ms < drain_ms && !cancelled; ms += 5) {
                        vTaskDelay(pdMS_TO_TICKS(5));
                    }
                    break;
                }
                if (cancelled) break;
                if (!starving) {
                    starving = true;
                    if (++underruns == 1) ESP_LOGW(LOG_TAG, "PCM buffer underrun");
                }
                if (sink.write(silence, sizeof(silence)) != sizeof(silence)) {
                    ESP_LOGE(LOG_TAG, "I2S write failed while buffering");
                    break;
                }
                continue;
            }

            starving = false;
            const uint8_t * pcm = psram + block.slot * BLOCK_BYTES;
            const size_t bytes = block.sectors * CDDA_SECTOR_BYTES;
            size_t written = 0;
            while (!cancelled && written < bytes) {
                const size_t count = sink.write(pcm + written, std::min<size_t>(CDDA_SECTOR_BYTES, bytes - written));
                if (!count) break;
                written += count;
                pcm_bytes += count;
            }
            xQueueSend(free_blocks, &block.slot, 0);
            if (!cancelled && written != bytes) {
                ESP_LOGE(LOG_TAG, "I2S write stopped after %zu/%zu bytes", written, bytes);
                break;
            }
        }
        cancelled = true;
        state = AudioStatus::PlayState::Stopped;
        ESP_LOGI(LOG_TAG, "Played %u PCM bytes, %u buffer underruns", pcm_bytes.load(), underruns);
    }
}
