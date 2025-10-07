#pragma once
#include <AudioTools.h>
#include <AudioTools/AudioCodecs/CodecHelix.h>
#include <AudioTools/AudioCodecs/ContainerM4A.h>
#include <AudioTools/AudioCodecs/CodecALAC.h>

/// Size of compressed data buffer in PS-RAM
const size_t NET_DATA_BUFFER_SIZE = 256 * 1024;

class FTPStreamingPipeline {
public:
  std::function<void(MetaDataType, const std::string)> metadataCallback = nullptr;

  FTPStreamingPipeline(AudioStream * outputPort, float netDataReqPct = 90.0):
    netDataBorder(netDataReqPct),
    inPort(nullptr),
    outPort(outputPort),
    bufferNetData(NET_DATA_BUFFER_SIZE),
    queueNetData(bufferNetData),
    splitToMeta(),
    decoder(outPort, &codec),
    copierDownloading(queueNetData, *inPort),
    codecTask("NETDEC", 40000, 6, 1),
    netTask("NETNET", 20000, 14, 0),
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
        codec.addDecoder(alac, "audio/alac");
        
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
    running = false;
    
    ESP_LOGI(LOG_TAG, "Finalizing codec task");
    xSemaphoreTake(semaCodec, portMAX_DELAY);
    codecTask.end();

    ESP_LOGI(LOG_TAG, "Finalizing net task");
    xSemaphoreTake(semaNet, portMAX_DELAY);
    netTask.end();

    ESP_LOGI(LOG_TAG, "Ending copiers");
    copierDecoding.end();
    copierDownloading.end();

    ESP_LOGI(LOG_TAG, "Ending queue");
    queueNetData.end();
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

    codecTask.begin([this]() { 
      xSemaphoreTake(semaCodec, portMAX_DELAY);
      while(bufferNetData.levelPercent() < netDataBorder && running) { 
          delay(100);
      }

      while(running) {
          copierDecoding.copy();
          delay(5);
      }

      ESP_LOGI(LOG_TAG, "Finishing up codec task");
      ESP_LOGI(LOG_TAG, "Ending decoder");
      decoder.end();
      ESP_LOGI(LOG_TAG, "Ending codec");
      codec.end();
      xSemaphoreGive(semaCodec);
      vTaskDelay(portMAX_DELAY);
    });
  }


  void netLoop() {
    TickType_t last_successful_copy = xTaskGetTickCount();
    xSemaphoreTake(semaNet, portMAX_DELAY);
    while(running) {
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
    }
    ESP_LOGI(LOG_TAG, "Finishing up net task");
    xSemaphoreGive(semaNet);
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
  DecoderALAC alac;
  MultiDecoder codec;
  ContainerM4A m4a_decap;
  MetaDataOutput outMeta;
  MultiOutput splitToMeta;
  BufferRTOS<uint8_t> bufferNetData;
  QueueStream<uint8_t> queueNetData;
  StreamCopy copierDownloading;
  StreamCopy copierDecoding;
  EncodedAudioStream decoder;
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
