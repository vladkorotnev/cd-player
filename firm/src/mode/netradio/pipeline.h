#include <modes/netradio_mode.h>
#include "AudioTools.h"
#include <AudioTools/AudioCodecs/CodecHelix.h>
#include <CommonHelix.h>
#include <lwip/sockets.h>

class AllocatorFastRAM : public Allocator {
    void* do_allocate(size_t size) {
      void* result = nullptr;
      if (size == 0) size = 1;
      if (result == nullptr) result = heap_caps_calloc(1, size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
      if (result == nullptr) {
        LOGE("Internal-RAM alloc failed for %zu bytes", size);
        stop();
      }
      // initialize object
      memset(result, 0, size);
      return result;
    }
  };

static AllocatorFastRAM fastAlloc;

// Ah yes, the hellscape of embedded internet radio streaming!
// I haven't seen such challenges since doing demoscene on ZX Spectrum I think, where you had all the stuff with contiguous memory et al.
// Probably a lot of this shite could have been made easier by just supplying a hardware MP3/AAC decoder chip on the board. But ESPer 1.0 was not made by someone smart, it was made by me, so
// on top of ATAPI over I2C now we also have the wonders of software decoding of high bitrate internet streams.
//
// How this is setup now is basically:
// (Network) -> ICYStream -> Net Buffer (PSRAM) -> Decoder -> PCM Buffer (DRAM) -> Output nub of the AudioRouter
// The issue here is being able to copy everything in time from each end while leaving time for the inbuilt tasks of LWIP and WiFi drivers so we actually also get data from the network.
// On top of which we need to also watch out for excess DRAM usage, since the HTTPS stack for some reason dislikes PSRAM all too much. As long as we are hovering around 90k on the heap we seem to be fine.
//
// Biggest challenges were experienced with WaveRadio Network, e.g. https://provoda.ch. Their server hands out enormous chunks in the chunked HTTP stream or something? 
// NDR-1 from Germany was also somewhat challenging to get playing at a stable rate.

// These parameters have been fine tuned for the current situation of the firmware, they might fail once remote or other background processing is added.

/// Size of compressed data buffer in PS-RAM
const size_t NET_DATA_BUFFER_SIZE = 256 * 1024;
/// Size of PCM data buffer in DRAM
const size_t PCM_DATA_BUFFER_SIZE = 8 * 1024;
/// Percent of compressed data buffer that must be full to start playing
const float NET_DATA_ENOUGH_MARK_PCT = 50.0; // %
/// Timeout for net client
const int NET_CLIENT_TIMEOUT = 10000; // ms
/// Timeout to consider no copies from network a stall
const int NET_NO_DATA_TIMEOUT = 10000; // ms
/// Interval for printing periodic stats logs (buffer %'s etc)
const int LOG_STATS_INTERVAL = 3000; // ms

class InternetRadioMode::StreamingPipeline {
    public:
        StreamingPipeline(Platform::AudioRouter * router):
            bufferNetData(NET_DATA_BUFFER_SIZE),
            bufferPcmData(PCM_DATA_BUFFER_SIZE, 1, portMAX_DELAY, portMAX_DELAY, fastAlloc),
            queueNetData(bufferNetData),
            queuePcmData(bufferPcmData),
            activeCodec(nullptr),
            decoder(&queueNetData, activeCodec),
            copierDownloading(queueNetData, urlStream),
            sndTask("IRASND", 8192, 8, 0),
            codecTask("IRADEC", 40000, 6, 1),
            netTask("IRANET", 20000, 14, 1),
            copierDecoding(decoder, queueNetData),
            copierPlaying(*router->get_output_port(), queuePcmData) {
                outPort = router->get_output_port();
                semaSnd = xSemaphoreCreateBinary();
                semaNet = xSemaphoreCreateBinary();
                semaCodec = xSemaphoreCreateBinary();
                xSemaphoreGive(semaSnd);
                xSemaphoreGive(semaNet);
                xSemaphoreGive(semaCodec);
        }
    
        ~StreamingPipeline() {
            if(running) stop();
    
            vSemaphoreDelete(semaSnd);
            vSemaphoreDelete(semaNet);
            vSemaphoreDelete(semaCodec);
        }

        int getNetBufferHealth() {
            if(!running) return 100;
            return trunc(bufferNetData.levelPercent());
        }
    
        void start(const std::string url, std::function<void(bool)> loadingCallback) {
            running = true;
            loadingCallback(true);
            ESP_LOGI(LOG_TAG, "Streamer is starting");
            queueNetData.begin();
            queuePcmData.begin();
            netTask.begin([this, url, loadingCallback]() { 
                    TickType_t last_shart_time = xTaskGetTickCount();
                    
                    urlStream.setMetadataCallback(_update_meta_global);
                    urlStream.httpRequest().setTimeout(NET_CLIENT_TIMEOUT);
                    urlStream.httpRequest().header().setProtocol("HTTP/1.0"); // <- important, because chunked transfer of some servers seems to cause trouble with buffering!
                    urlStream.begin(url.c_str());
                    ESP_LOGI(LOG_TAG, "Streamer did begin URL");

                    const char * _srv_mime = urlStream.httpRequest().reply().get("Content-Type");
                    if(_srv_mime != nullptr) {
                        const std::string srv_mime = _srv_mime;
                        ESP_LOGI(LOG_TAG, "Server reports content-type: %s", srv_mime.c_str());
                        if(srv_mime == "audio/mpeg" || srv_mime == "audio/mp3") {
                            activeCodec = new MP3DecoderHelix();
                        }
                        else if(srv_mime == "audio/aac" || srv_mime == "audio/aacp" /* found on Keygen FM */) {
                            activeCodec = new AACDecoderHelix();
                        }
                        else {
                            ESP_LOGE(LOG_TAG, "Content-type is not supported, TODO show error");
                            loadingCallback(false);
                            vTaskDelay(portMAX_DELAY);
                        }
                        decoder.setDecoder(activeCodec);
                        activeCodec->setNotifyActive(true);
                        activeCodec->addNotifyAudioChange(*outPort);
                        decoder.setOutput(queuePcmData);
                    } else {
                        ESP_LOGE(LOG_TAG, "No content-type reported, TODO show error");
                        loadingCallback(false);
                        vTaskDelay(portMAX_DELAY);
                    }
                    decoder.setNotifyActive(true);
                    decoder.begin();
                    ESP_LOGI(LOG_TAG, "Streamer did begin decoder");
    
                    TickType_t last_successful_copy = xTaskGetTickCount();
                    TickType_t last_stats = xTaskGetTickCount();

                    codecTask.begin([this]() { 
                        while(bufferNetData.levelPercent() < NET_DATA_ENOUGH_MARK_PCT && running) { 
                            delay(125);
                        }
                        
                        if(!running) {
                            ESP_LOGI(LOG_TAG, "Finishing up codec task early");
                            decoder.end();
                            xSemaphoreGive(semaCodec);
                            vTaskDelay(portMAX_DELAY);
                        }
    
                        while(copierDecoding.copy() > 0 && !bufferNetData.isEmpty() && running) {
                            delay(10);
                        }
                        while(bufferNetData.levelPercent() < NET_DATA_ENOUGH_MARK_PCT && running) delay(250);

    
                        if(!running) {
                            ESP_LOGI(LOG_TAG, "Finishing up codec task");
                            decoder.end();
                            xSemaphoreGive(semaCodec);
                            vTaskDelay(portMAX_DELAY);
                        }

                        delay(5);
                        xSemaphoreGive(semaCodec);
                    });

                    sndTask.begin([this, loadingCallback]() { 
                        xSemaphoreTake(semaSnd, portMAX_DELAY);
                        if(!copierPlaying.copy() && running) {
                            ESP_LOGD(LOG_TAG, "copierPlaying underrun!? PCM buffer %.00f%%, net buffer %.00f%%", bufferPcmData.levelPercent(), bufferNetData.levelPercent()); 
                            loadingCallback(true);
                            while(bufferPcmData.isEmpty() && running) { 
                                xSemaphoreGive(semaSnd);
                                delay(100);
                                xSemaphoreTake(semaSnd, portMAX_DELAY);
                            }
                            loadingCallback(false);
                        }
                        xSemaphoreGive(semaSnd);
                        delay(2); 
                        if(!running) { 
                            ESP_LOGI(LOG_TAG, "Finishing up sound task");
                            vTaskDelay(portMAX_DELAY);
                        }
                    });

                    while(true) {
                        xSemaphoreTake(semaNet, portMAX_DELAY);
                        TickType_t now = xTaskGetTickCount();
                        int copied = copierDownloading.copy();
                        if(copied > 0) {
                            last_successful_copy = now;
                            delay(2);
                        } else if(now - last_successful_copy >= pdTICKS_TO_MS(NET_NO_DATA_TIMEOUT)  && (bufferNetData.levelPercent() < 20.0 || bufferPcmData.isEmpty())) {
                            ESP_LOGE(LOG_TAG, "streamer is stalled!? time since last shart = [ %i ms ]", now - last_shart_time);
                            last_shart_time = now;
                            loadingCallback(true);
                            urlStream.end();
                            int retryDelay = 100;
                            do {
                                if(bufferPcmData.isEmpty()) bufferNetData.clear(); // prevent click artifact on restart playback
                                delay(retryDelay);
                                retryDelay += 1000;
                                if(retryDelay > NET_CLIENT_TIMEOUT) retryDelay = NET_CLIENT_TIMEOUT;
                                ESP_LOGE(LOG_TAG, "streamer is trying to begin URL again");
                            } while(!urlStream.begin(url.c_str()));
                            last_successful_copy = now;
                        } else if(copied < 0) {
                            ESP_LOGE(LOG_TAG, "copied = %i ???", copied);
                        } else if(bufferNetData.isFull()) {
                            ESP_LOGW(LOG_TAG, "Net buffer full, no space to receive more data!!");
                            delay(10);
                        } else {
                            // copied nothing, maybe yield to another task
                            delay(10);
                        }
                        if(now - last_stats >= pdTICKS_TO_MS(LOG_STATS_INTERVAL)) {
                            ESP_LOGI(LOG_TAG, "Stats: PCM buffer %.00f%%, net buffer %.00f%%", bufferPcmData.levelPercent(), bufferNetData.levelPercent()); 
                            last_stats = now;
                        }

                        if(!running) {
                            urlStream.end();
                            xSemaphoreGive(semaNet);
                            break;
                        }

                        xSemaphoreGive(semaNet);
                    }
                    ESP_LOGI(LOG_TAG, "Finishing up net task");
                    vTaskDelay(portMAX_DELAY);
                }
            );
        }
    
        void stop() {
            NullStream tmp;
            decoder.setOutput(tmp);
            if(activeCodec != nullptr) {
                activeCodec->setOutput(tmp);
            }

            running = false;
            xSemaphoreTake(semaNet, portMAX_DELAY);
            ESP_LOGI(LOG_TAG, "Streamer finalizing net task");
            netTask.end();
    
            ESP_LOGI(LOG_TAG, "Streamer finalizing sound task");
            xSemaphoreTake(semaSnd, portMAX_DELAY);
            sndTask.end();
    
            // Small shitshow because the codec thread sometimes gets stuck and never exits and leaves the semaphore
            ESP_LOGI(LOG_TAG, "Streamer finalizing codec task");
            bool codecNotStuck = xSemaphoreTake(semaCodec, pdMS_TO_TICKS(1000));
            codecTask.end();

            if(!codecNotStuck) { 
                delay(1000); // give it time to unshit itself to prevent explosions 
            }
            if(activeCodec != nullptr) {
                activeCodec->setOutput(tmp);
                delete activeCodec;
                activeCodec = nullptr;
            }
        }
    
    protected:
        bool running = false;
        ICYStream urlStream;
        StreamCopy copierDownloading;
        StreamCopy copierPlaying;
        StreamCopy copierDecoding;
        AudioDecoder * activeCodec;
        EncodedAudioStream decoder;
        Task sndTask;
        Task codecTask;
        Task netTask;
        BufferRTOS<uint8_t> bufferNetData;
        BufferRTOS<uint8_t> bufferPcmData;
        QueueStream<uint8_t> queueNetData;
        QueueStream<uint8_t> queuePcmData;
        AudioStream * outPort;
        SemaphoreHandle_t semaSnd;
        SemaphoreHandle_t semaNet;
        SemaphoreHandle_t semaCodec;

        const char * LOG_TAG = "ISTREAM";
    };