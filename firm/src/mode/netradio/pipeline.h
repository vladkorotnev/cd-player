#include <modes/netradio_mode.h>
#include "AudioTools.h"
#include <AudioTools/AudioCodecs/CodecHelix.h>
#include <CommonHelix.h>
#include <lwip/sockets.h>

class InternetRadioMode::StreamingPipeline {
    public:
        StreamingPipeline(Platform::AudioRouter * router):
            bufferNetData(512 * 1024),
            bufferPcmData(128 * 1024),
            queueNetData(bufferNetData),
            queuePcmData(bufferPcmData),
            urlStream(),
            activeCodec(nullptr),
            decoder(&queueNetData, activeCodec),
            sndTask("IRASND", 8192, 2, 0),
            codecTask("IRADEC", 40000, 8, 1),
            netTask("IRANET", 20000, 14, 1),
            copierDownloading(queueNetData, urlStream),
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
    
        void start(const std::string url, std::function<void(bool)> loadingCallback) {
            running = true;
            loadingCallback(true);
            ESP_LOGI(LOG_TAG, "Streamer is starting");
            queueNetData.begin();
            queuePcmData.begin();
            netTask.begin([this, url, loadingCallback]() { 
                    urlStream.setMetadataCallback(_update_meta_global);
                    urlStream.setReadBufferSize(4096);
                    urlStream.begin(url.c_str());
                    ESP_LOGI(LOG_TAG, "Streamer did begin URL");

                    const char * _srv_mime = urlStream.httpRequest().reply().get("Content-Type");
                    netBufferLowWaterMark = 20.0; // essentially start with a small buffer to get a quick start, and have it grow when a hiccup occurs
                    if(_srv_mime != nullptr) {
                        const std::string srv_mime = _srv_mime;
                        ESP_LOGI(LOG_TAG, "Server reports content-type: %s", srv_mime.c_str());
                        if(srv_mime == "audio/mpeg" || srv_mime == "audio/mp3") {
                            activeCodec = new MP3DecoderHelix();
                        }
                        else if(srv_mime == "audio/aac") {
                            activeCodec = new AACDecoderHelix();
                        }
                        else {
                            ESP_LOGE(LOG_TAG, "Content-type is not supported, TODO show error");
                            vTaskDelay(portMAX_DELAY);
                        }
                        decoder.setDecoder(activeCodec);
                        activeCodec->setNotifyActive(true);
                        activeCodec->addNotifyAudioChange(*outPort);
                        decoder.setOutput(queuePcmData);
                    } else {
                        ESP_LOGE(LOG_TAG, "No content-type reported, TODO show error");
                        vTaskDelay(portMAX_DELAY);
                    }
                    decoder.setNotifyActive(true);
                    decoder.begin();
                    ESP_LOGI(LOG_TAG, "Streamer did begin decoder");
    
                    TickType_t last_successful_copy = xTaskGetTickCount();
                    TickType_t last_stats = xTaskGetTickCount();

                    codecTask.begin([this, loadingCallback]() { 
                        xSemaphoreTake(semaCodec, portMAX_DELAY);
                        while(bufferNetData.levelPercent() < (netBufferLowWaterMark+1) && running) { 
                            delay(125);
                        }
                        
                        if(!running) {
                            ESP_LOGI(LOG_TAG, "Finishing up codec task early");
                            decoder.end();
                            xSemaphoreGive(semaCodec);
                            vTaskDelay(portMAX_DELAY);
                        }
    
                        while(running && copierDecoding.copy() > 0) {
                            delay(25);
                        }
    
                        if(!running) {
                            ESP_LOGI(LOG_TAG, "Finishing up codec task");
                            decoder.end();
                            xSemaphoreGive(semaCodec);
                            vTaskDelay(portMAX_DELAY);
                        }

                        delay(20);
                        xSemaphoreGive(semaCodec);
                    });

                    sndTask.begin([this, loadingCallback]() { 
                        xSemaphoreTake(semaSnd, portMAX_DELAY);
                        if(!copierPlaying.copy() && running) {
                            ESP_LOGE(LOG_TAG, "copierPlaying underrun!? PCM buffer %.00f%%, net buffer %.00f%%", bufferPcmData.levelPercent(), bufferNetData.levelPercent()); 
                            netBufferLowWaterMark = std::min(99.0f, netBufferLowWaterMark + 10.0f);
                            while(bufferPcmData.levelPercent() < 25.0f && running) { 
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

                    while(running) {
                        xSemaphoreTake(semaNet, portMAX_DELAY);
                        TickType_t now = xTaskGetTickCount();
                        int copied = copierDownloading.copy();
                        if(copied > 0) {
                            last_successful_copy = now;
                        } else if(now - last_successful_copy >= pdTICKS_TO_MS(2500) /* && !bufferNetData.isFull() */) {
                            ESP_LOGE(LOG_TAG, "streamer is stalled!?");
                            if(bufferNetData.isEmpty() || bufferPcmData.isEmpty()) {
                                netBufferLowWaterMark = std::min(99.0f, netBufferLowWaterMark + 10.0f);
                                bufferNetData.clear();
                                loadingCallback(true);
                            }
                            urlStream.end();
                            int retryDelay = 100;
                            do {
                                delay(retryDelay);
                                retryDelay += 1000;
                                ESP_LOGE(LOG_TAG, "streamer is trying to begin URL again");
                            } while(!urlStream.begin(url.c_str()));
                            last_successful_copy = now;
                        } else if(copied < 0) {
                            ESP_LOGE(LOG_TAG, "copied = %i ???", copied);
                        }
                        if(now - last_stats >= pdTICKS_TO_MS(1000)) {
                            ESP_LOGI(LOG_TAG, "Stats: PCM buffer %.00f%%, net buffer %.00f%%", bufferPcmData.levelPercent(), bufferNetData.levelPercent()); 
                            last_stats = now;
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
            ESP_LOGI(LOG_TAG, "Streamer finalizing net task");
            xSemaphoreTake(semaNet, portMAX_DELAY);
            netTask.end();
    
            ESP_LOGI(LOG_TAG, "Streamer finalizing url");
            urlStream.end();
    
            ESP_LOGI(LOG_TAG, "Streamer finalizing sound task");
            xSemaphoreTake(semaSnd, portMAX_DELAY);
            sndTask.end();
    
            // Small shitshow because the codec thread sometimes gets stuck and never exits and leaves the semaphore
            ESP_LOGI(LOG_TAG, "Streamer finalizing codec task");
            queueNetData.clear();
            bufferNetData.clear();
            bufferPcmData.clear();
            queuePcmData.clear();
            decoder.end();
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
            
            ESP_LOGI(LOG_TAG, "Streamer finalizing buffers");    
            queuePcmData.end();
            queueNetData.end();
        }
    
    protected:
        bool running = false;
        float netBufferLowWaterMark = 5.0;
        StreamCopy copierPlaying;
        StreamCopy copierDecoding;
        StreamCopy copierDownloading;
        AudioDecoder * activeCodec;
        EncodedAudioStream decoder;
        ICYStream urlStream;
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