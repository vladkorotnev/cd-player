#include <modes/netradio_mode.h>
#include "AudioTools.h"
#include <AudioTools/AudioCodecs/CodecHelix.h>
#include <CommonHelix.h>
#include <lwip/sockets.h>
#include <utils.h>

// Ah yes, the hellscape of embedded internet radio streaming!
// I haven't seen such challenges since doing demoscene on ZX Spectrum I think, where you had all the stuff with contiguous memory et al.
// Probably a lot of this shite could have been made easier by just supplying a hardware MP3/AAC decoder chip on the board. But ESPer 1.0 was not made by someone smart, it was made by me, so
// on top of ATAPI over I2C now we also have the wonders of software decoding of high bitrate internet streams.
//
// How this is setup now is basically:
// (Network) -> ICYStream -> Net Buffer (PSRAM) -> Decoder -> Output nub of the AudioRouter
// The issue here is being able to copy everything in time from each end while leaving time for the inbuilt tasks of LWIP and WiFi drivers so we actually also get data from the network.
// On top of which we need to also watch out for excess DRAM usage, since the HTTPS stack for some reason dislikes PSRAM all too much. As long as we are hovering around 90k on the heap we seem to be fine.
//
// Biggest challenges were experienced with WaveRadio Network, e.g. https://provoda.ch. Their server hands out enormous chunks in the chunked HTTP stream or something? 
// NDR-1 from Germany was also somewhat challenging to get playing at a stable rate.

// These parameters have been fine tuned for the current situation of the firmware, they might fail once remote or other background processing is added.

/// Size of compressed data buffer in PS-RAM
const size_t NET_DATA_BUFFER_SIZE = 256 * 1024;
/// Percent of compressed data buffer that must be full to start playing
const float NET_DATA_ENOUGH_MARK_PCT = 35.0; // %
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
            queueNetData(bufferNetData),
            kbpsGauge(queueNetData),
            activeCodec(nullptr),
            decoder(&queueNetData, activeCodec),
            copierDownloading(kbpsGauge, urlStream),
            codecTask("IRADEC", 8000, 4, 1),
            netTask("IRANET", 6000, 8, 0),
            copierDecoding(decoder, queueNetData) {
                outPort = router->get_io_port_nub();
                semaNet = xSemaphoreCreateBinary();
                semaCodec = xSemaphoreCreateBinary();
                xSemaphoreGive(semaNet);
                xSemaphoreGive(semaCodec);
        }
    
        ~StreamingPipeline() {
            if(running) { 
                ESP_LOGI(LOG_TAG, "Stop before destroying pipeline");
                stop();
            }
            ESP_LOGI(LOG_TAG, "Finalizing pipeline");
            vSemaphoreDelete(semaNet);
            vSemaphoreDelete(semaCodec);
        }

        int getNetBufferHealth() {
            if(!running) return 100;
            return trunc(bufferNetData.levelPercent());
        }

        int getNetBufferVolume() {
            if(!running) return 0;
            return bufferNetData.available(); // "available to read". I was confused at first as well.
        }

        int getEnoughMark() {
            return trunc(enoughMark);
        }

        int getBps() {
            return kbpsGauge.bytesPerSecond();
        }

        int getStreamBitrate() {
            if (activeCodec == &mp3) {
                MP3FrameInfo fi = mp3.audioInfoEx();
                return fi.bitrate;
            } else if (activeCodec == &aac) {
                AACFrameInfo fi = aac.audioInfoEx();
                return fi.bitRate;
            } else {
                return 0;
            }
        }

        const std::string& getCodecName() {
            return codecName;
        }
    
        void start(const std::string url, std::function<void(bool)> loadingCallback) {
            running = true;
            loadingCallback(true);
            ESP_LOGI(LOG_TAG, "Streamer is starting");
            queueNetData.begin();
            netTask.begin([this, url, loadingCallback]() { 
                    xSemaphoreTake(semaNet, portMAX_DELAY);
                    TickType_t last_shart_time = xTaskGetTickCount();
                    
                    urlStream.setMetadataCallback(_update_meta_global);
                    urlStream.httpRequest().setTimeout(NET_CLIENT_TIMEOUT);
                    urlStream.setTimeout(NET_CLIENT_TIMEOUT);
                    urlStream.setConnectionClose(false);
                    urlStream.httpRequest().header().setProtocol("HTTP/1.0"); // <- important, because chunked transfer of some servers seems to cause trouble with buffering!
                    urlStream.begin(url.c_str());
                    ESP_LOGI(LOG_TAG, "Streamer did begin URL");
                    urlStream.waitForData(NET_CLIENT_TIMEOUT);

                    const char * _srv_mime = urlStream.httpRequest().reply().get("Content-Type");
                    if(_srv_mime != nullptr) {
                        const std::string srv_mime = _srv_mime;
                        ESP_LOGI(LOG_TAG, "Server reports content-type: %s", srv_mime.c_str());
                        if(srv_mime == "audio/mpeg" || srv_mime == "audio/mp3") {
                            activeCodec = &mp3;
                            codecName = "MP3";
                            ESP_LOGI(LOG_TAG, "Using MP3/Helix");
                        }
                        else if(srv_mime == "audio/aac" || srv_mime == "audio/aacp" /* found on Keygen FM */) {
                            activeCodec = &aac;
                            codecName = "AAC";
                            ESP_LOGI(LOG_TAG, "Using AAC/Helix");
                        }
                        else {
                            ESP_LOGE(LOG_TAG, "Content-type is not supported, TODO show error");
                            loadingCallback(false);
                            vTaskDelay(portMAX_DELAY);
                        }
                        decoder.setDecoder(activeCodec);
                        activeCodec->setNotifyActive(true);
                        activeCodec->addNotifyAudioChange(*outPort);
                        decoder.setOutput(*outPort);
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

                    codecTask.begin([this, loadingCallback]() {
                        xSemaphoreTake(semaCodec, portMAX_DELAY);
                        while(running) {
                            while(bufferNetData.levelPercent() < enoughMark && running) { 
                                if (!didAutoTuneEnoughMark) {
                                    delay(500);
                                    int bytes_per_second = getBps();
                                    if (bytes_per_second > 0) {
                                        enoughMark = std::min((((float)bytes_per_second * 5.0)/(float)NET_DATA_BUFFER_SIZE) * 100.0, 80.0);
                                        ESP_LOGI(LOG_TAG, "Autotune enough mark for %s/s: %f", format_bytes(bytes_per_second).c_str(), enoughMark);
                                    }
                                }
                                delay(125);
                            }
                            didAutoTuneEnoughMark = true;

                            loadingCallback(false);
                            while(copierDecoding.copy() > 0 && !bufferNetData.isEmpty() && running) {
                                delay(20);
                            }
                            loadingCallback(true);
                            enoughMark = min(enoughMark + 5.0, 90.0);
                            ESP_LOGW(LOG_TAG, "Hiccup? new enoughMark=[%f]", enoughMark);
                            delay(5);
                        }

                        ESP_LOGW(LOG_TAG, "Codec task is wrapping up");
                        ESP_LOGI(LOG_TAG, "Ending decoder");
                        decoder.end();

                        ESP_LOGI(LOG_TAG, "Active codec = [%p]", activeCodec);
                        if(activeCodec != nullptr) {
                            ESP_LOGI(LOG_TAG, "[%p]->end()", activeCodec);
                            activeCodec->end();
                            ESP_LOGI(LOG_TAG, "[%p]-end()'ed", activeCodec);
                            activeCodec = nullptr;
                            ESP_LOGI(LOG_TAG, "Ended codec = [%p]", activeCodec);
                        }
                        xSemaphoreGive(semaCodec);
                        ESP_LOGW(LOG_TAG, "Codec task is DONE!");
                        vTaskDelay(portMAX_DELAY);
                    });

                    while(running) {
                        TickType_t now = xTaskGetTickCount();
                        int copied = copierDownloading.copy();
                        if(copied > 0) {
                            last_successful_copy = now;
                            delay(15);
                        } else if(now - last_successful_copy >= pdTICKS_TO_MS(NET_NO_DATA_TIMEOUT)  && (bufferNetData.levelPercent() < 20.0)) {
                            ESP_LOGE(LOG_TAG, "streamer is stalled!? time since last shart = [ %i ms ]", now - last_shart_time);
                            last_shart_time = now;
                            loadingCallback(true);
                            urlStream.end();
                            int retryDelay = 100;
                            do {
                                bufferNetData.clear(); // prevent click artifact on restart playback
                                delay(retryDelay);
                                retryDelay += 1000;
                                if(retryDelay > NET_CLIENT_TIMEOUT) retryDelay = NET_CLIENT_TIMEOUT;
                                ESP_LOGE(LOG_TAG, "streamer is trying to begin URL again");
                            } while(!urlStream.begin(url.c_str()) && running);
                            last_successful_copy = now;
                        } else if(copied < 0) {
                            ESP_LOGE(LOG_TAG, "copied = %i ???", copied);
                        } else if(bufferNetData.isFull()) {
                            ESP_LOGW(LOG_TAG, "Net buffer full, no space to receive more data!!");
                            delay(10);
                        } else {
                            // copied nothing, maybe yield to another task
                            delay(15);
                        }
                        if(now - last_stats >= pdTICKS_TO_MS(LOG_STATS_INTERVAL)) {
                            ESP_LOGV(LOG_TAG, "Stats: net buffer %.00f%%", bufferNetData.levelPercent()); 
                            last_stats = now;
                        }
                    }
                    ESP_LOGI(LOG_TAG, "Finishing up net task");
                    xSemaphoreGive(semaNet);
                    vTaskDelay(portMAX_DELAY);
                }
            );
        }
    
        void stop() {
            running = false;
            xSemaphoreTake(semaNet, portMAX_DELAY);
            ESP_LOGI(LOG_TAG, "Finalizing net task");
            netTask.end();
    
            ESP_LOGI(LOG_TAG, "Finalizing codec task");
            xSemaphoreTake(semaCodec, portMAX_DELAY);

            ESP_LOGI(LOG_TAG, "Ending copiers");
            copierDecoding.end();
            copierDownloading.end();

            ESP_LOGI(LOG_TAG, "Ending URLStream");
            urlStream.end();

            ESP_LOGI(LOG_TAG, "Ending queue");
            queueNetData.end();
        }
    
    protected:
        bool running = false;
        float enoughMark = NET_DATA_ENOUGH_MARK_PCT;
        int didAutoTuneEnoughMark = false;
        std::string codecName = "?";
        AudioStream * outPort;
        MP3DecoderHelix mp3;
        AACDecoderHelix aac;
        AudioDecoder * activeCodec;
        SemaphoreHandle_t semaNet;
        SemaphoreHandle_t semaCodec;
        BufferRTOS<uint8_t> bufferNetData;
        QueueStream<uint8_t> queueNetData;
        MeasuringStream kbpsGauge;
        ICYStream urlStream;
        EncodedAudioStream decoder;
        StreamCopy copierDownloading;
        StreamCopy copierDecoding;
        Task codecTask;
        Task netTask;

        const char * LOG_TAG = "ISTREAM";
    };