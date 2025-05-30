#pragma once
#include <AudioTools.h>
#include <AudioTools/AudioCodecs/CodecHelix.h>
#include <AudioTools/AudioCodecs/ContainerM4A.h>

/// Size of compressed data buffer in PS-RAM
const size_t NET_DATA_BUFFER_SIZE = 256 * 1024;

class FTPStreamingPipeline {
public:
  std::function<void(MetaDataType, const std::string)> metadataCallback = nullptr;

  FTPStreamingPipeline(AudioStream * outputPort, float netDataReqPct = 90.0, int decoderCore = 0):
    netDataBorder(netDataReqPct),
    inPort(nullptr),
    outPort(outputPort),
    bufferNetData(NET_DATA_BUFFER_SIZE),
    queueNetData(bufferNetData),
    splitToMeta(),
    decoder(outPort, &codec),
    copierDownloading(queueNetData, *inPort),
    codecTask("NETDEC", 40000, 6, decoderCore),
    netTask("NETNET", 20000, 14, 1),
    m4a_decap(codec),
    copierDecoding(splitToMeta, queueNetData) {
        outPort = outputPort;
        semaNet = xSemaphoreCreateBinary();
        semaCodec = xSemaphoreCreateBinary();
        xSemaphoreGive(semaNet);
        xSemaphoreGive(semaCodec);

        codec.addDecoder(mp3, "audio/mpeg");
        codec.addDecoder(aac, "audio/aac");
        codec.addDecoder(wav, "audio/vnd.wave");
        codec.addDecoder(m4a_decap, "audio/m4a");
        
        splitToMeta.add(decoder);
        splitToMeta.add(outMeta);

        outMeta.setCallback(printMetaData);
        outMeta.begin();

        decoder.setDecoder(&codec);
        codec.setNotifyActive(true);
        codec.addNotifyAudioChange(*outPort);
        decoder.setNotifyActive(true);
        decoder.addNotifyAudioChange(*outPort);

        decoder.setOutput(*outPort);
        _that = this;
  }

  virtual ~FTPStreamingPipeline() {
    if(running) stop();

    vSemaphoreDelete(semaNet);
    vSemaphoreDelete(semaCodec);
    _that = nullptr;
  }

  void start(Stream * in) {
    setSource(in);
    running = true;
    queueNetData.begin();
    ESP_LOGI(LOG_TAG, "Streaming is starting");
    netTask.begin([this, in]() {                     
        startDecoding();
        netLoop();
      }
    );
  }

  void stop() {
    NullStream tmp;
    decoder.setOutput(tmp);
    codec.setOutput(tmp);
    codec.end();
    decoder.end();

    running = false;
    xSemaphoreTake(semaNet, pdMS_TO_TICKS(1000));
    ESP_LOGI(LOG_TAG, "Streamer finalizing net task");
    netTask.end();

    // Small shitshow because the codec thread sometimes gets stuck and never exits and leaves the semaphore
    ESP_LOGI(LOG_TAG, "Streamer finalizing codec task");
    bool codecNotStuck = xSemaphoreTake(semaCodec, pdMS_TO_TICKS(1000));
    codecTask.end();

    if(!codecNotStuck) { 
        delay(1000); // give it time to unshit itself to prevent explosions 
    }
  }

  int getNetBufferHealth() {
    if(!running) return 100;
    return trunc(bufferNetData.levelPercent());
  }

protected:
  bool running = false;
  Task netTask;

  void setSource(Stream * source) {
    inPort = source;
    copierDownloading.begin(queueNetData, *inPort);
  }

  void startDecoding() {
    decoder.setNotifyActive(true);
    decoder.begin();
    ESP_LOGI(LOG_TAG, "Streamer did begin decoder");

    TickType_t last_successful_copy = xTaskGetTickCount();
    TickType_t last_stats = xTaskGetTickCount();

    codecTask.begin([this]() { 
      while(bufferNetData.levelPercent() < netDataBorder && running) { 
          delay(1);
      }
      
      if(!running) {
          ESP_LOGI(LOG_TAG, "Finishing up codec task early");
          decoder.end();
          xSemaphoreGive(semaCodec);
          vTaskDelay(portMAX_DELAY);
      }

      while(running) {
          copierDecoding.copy();
          delay(1);
      }

      ESP_LOGI(LOG_TAG, "Finishing up codec task");
      decoder.end();
      xSemaphoreGive(semaCodec);
      vTaskDelay(portMAX_DELAY);
    });
  }


  void netLoop() {
    TickType_t last_successful_copy = xTaskGetTickCount();
    while(true) {
      xSemaphoreTake(semaNet, portMAX_DELAY);
      TickType_t now = xTaskGetTickCount();
      int copied = copierDownloading.copy();
      if(copied > 0) {
          last_successful_copy = now;
          delay(2);
      } else if(copied < 0) {
          ESP_LOGE(LOG_TAG, "copied = %i ???", copied);
      } else if(bufferNetData.isFull()) {
          ESP_LOGW(LOG_TAG, "Net buffer full, no space to receive more data!!");
          delay(10);
      } else {
          // copied nothing, maybe yield to another task
          delay(10);
      }

      if(!running) {
          xSemaphoreGive(semaNet);
          break;
      }

      xSemaphoreGive(semaNet);
    }
    ESP_LOGI(LOG_TAG, "Finishing up net task");
    vTaskDelay(portMAX_DELAY);
  }

  void processMetadata(MetaDataType kind, const std::string value) {
    ESP_LOGI(LOG_TAG, "Meta[%s] = [%s]", toStr(kind), value.c_str());
    if (metadataCallback) metadataCallback(kind, value);
  }
private:
  Stream * inPort;
  MP3DecoderHelix mp3;
  AACDecoderHelix aac;
  WAVDecoder wav;
  MultiDecoder codec;
  ContainerM4A m4a_decap;
  MetaDataOutput outMeta;
  MultiOutput splitToMeta;
  StreamCopy copierDownloading;
  StreamCopy copierDecoding;
  EncodedAudioStream decoder;
  BufferRTOS<uint8_t> bufferNetData;
  QueueStream<uint8_t> queueNetData;
  AudioStream * outPort;
  SemaphoreHandle_t semaNet;
  SemaphoreHandle_t semaCodec;
  Task codecTask;
  float netDataBorder;
  static FTPStreamingPipeline * _that;

  static void printMetaData(MetaDataType type, const char* str, int len){
    if (_that) {
      _that->processMetadata(type, str);
    }
  }

  const char * LOG_TAG = "FTPStreamer";
};

FTPStreamingPipeline* FTPStreamingPipeline::_that = nullptr;
