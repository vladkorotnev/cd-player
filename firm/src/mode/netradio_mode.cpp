#include <modes/netradio_mode.h>
#include <esper-gui/views/framework.h>
#include <views/wifi_icon.h>
#include "AudioTools.h"
#include <AudioTools/AudioCodecs/CodecHelix.h>

using std::make_shared;
using std::shared_ptr;

static const char LOG_TAG[] = "APL_IRA";

class ChannelGridBar: public UI::View {
public:
    ChannelGridBar(EGRect f, int channel_count = 6): View(f) {
        auto font = Fonts::TinyDigitFont;
        int channel_width = frame.size.width / channel_count;
        int channel_margin = channel_width/2 - font->size.width/2; // assuming one digit

        for(int i = 1; i <= channel_count; i++) {
            auto lbl = make_shared<UI::Label>(UI::Label({{(i-1)*channel_width + channel_margin, 0}, {font->size.width, font->size.height}}, font, UI::Label::Alignment::Center, std::to_string(i)));
            subviews.push_back(lbl);
        }
    }

    void render(EGGraphBuf * buf) override {
        for(int i = 0; i < subviews.size(); i++) {
            auto lbl = subviews[i];
            if(i == active_ch_idx) {
                EGDrawLine(buf, {line_left_cur, lbl->frame.origin.y + lbl->frame.size.height + 1}, {line_right_cur, lbl->frame.origin.y + lbl->frame.size.height + 1});
            }
        }
        View::render(buf);
    }

    bool needs_display() override {
        if(line_right_tgt != line_right_cur || line_left_tgt != line_left_cur) {
            bool going_right = (line_right_tgt > line_right_cur || line_left_tgt > line_left_cur);
            int* first_coord = going_right ? &line_right_cur : &line_left_cur;
            int* first_coord_tgt = going_right ? &line_right_tgt : &line_left_tgt;
            int* second_coord = !going_right ? &line_right_cur : &line_left_cur;
            int* second_coord_tgt = !going_right ? &line_right_tgt : &line_left_tgt;

            if(*first_coord_tgt > *first_coord) {
                *first_coord += std::min(line_speed/2, *first_coord_tgt - *first_coord);
                line_speed = std::min(32, line_speed+1);
            }
            else if(*first_coord_tgt < *first_coord) {
                *first_coord -= std::min(line_speed/2, *first_coord - *first_coord_tgt);
                line_speed = std::min(32, line_speed+1);
            }
            else if(*second_coord_tgt > *second_coord) {
                *second_coord += std::min(line_speed/2, *second_coord_tgt - *second_coord);
                line_speed = std::max(2, line_speed - 1);
            }
            else if(*second_coord_tgt < *second_coord) {
                *second_coord -= std::min(line_speed/2, *second_coord - *second_coord_tgt);
                line_speed = std::max(2, line_speed - 1);
            }

            set_needs_display();
        }
        return View::needs_display();
    }

    void set_active_ch_idx(int num) {
        active_ch_idx = num;
        if(num < 0 || num > subviews.size()) {
            line_left_cur = 0;
            line_right_cur = 0;
            line_left_tgt = 0;
            line_right_tgt = 0;
            set_needs_display();
            return;
        }

        if(line_left_cur == line_left_tgt && line_right_cur == line_right_tgt) line_speed = 2;
        auto lbl = subviews[num];
        line_left_tgt = lbl->frame.origin.x - 1;
        line_right_tgt = lbl->frame.origin.x + lbl->frame.size.width + 1;
        set_needs_display();
    }

private:
    int active_ch_idx = -1;
    int line_speed = 2;
    
    int line_left_cur = 0;
    int line_right_cur = 0;
    int line_left_tgt = 0;
    int line_right_tgt = 0;
};

class InternetRadioMode::InternetRadioView: public UI::View {
public:
    shared_ptr<WiFiIcon> wifi;
    shared_ptr<UI::TinySpinner> loading;

    shared_ptr<UI::Label> lblTitle;
    shared_ptr<UI::Label> lblSubtitle;

    shared_ptr<ChannelGridBar> channelBar;

    InternetRadioView(EGRect f): View(f) {
        wifi = make_shared<WiFiIcon>(WiFiIcon({{0, 32 - 7}, {5, 5}}));
        loading = make_shared<UI::TinySpinner>(UI::TinySpinner({{160 - 6, 32 - 7}, {5, 5}}));
        loading->hidden = true;

        channelBar = make_shared<ChannelGridBar>(ChannelGridBar({{5, 32 - 7}, {160 - 12, 7}}));

        lblTitle = make_shared<UI::Label>(UI::Label({{0, 8}, {160, 16}}, Fonts::FallbackWildcard16px, UI::Label::Alignment::Center));
        lblSubtitle = make_shared<UI::Label>(UI::Label({{0, 0}, {160, 8}}, Fonts::FallbackWildcard8px, UI::Label::Alignment::Center));

        lblTitle->auto_scroll = true;
        lblTitle->synchronize_scrolling_to(&lblSubtitle);
        lblSubtitle->auto_scroll = true;
        lblSubtitle->synchronize_scrolling_to(&lblTitle);

        subviews.push_back(wifi);
        subviews.push_back(channelBar);
        subviews.push_back(loading);
        subviews.push_back(lblTitle);
        subviews.push_back(lblSubtitle);
    }

    void update_meta(MetaDataType type, const char * str, int len) {
        if(type == MetaDataType::Title) {
            has_metadata = true;
            lblSubtitle->set_value(station);
            lblTitle->set_value(str);
        }
        else if(type == MetaDataType::Name) {
            // usually just the station ID
            station = str;
            if(has_metadata) {
                lblSubtitle->set_value(station);
            } 
            else {
                lblTitle->set_value(station);
            }
        }
    }

    void reset_meta(const std::string station_name) {
        lblSubtitle->set_value("");
        lblTitle->set_value(station_name);
        station = station_name;
        has_metadata = false;
    }

    void set_loading(bool isLoading) {
        loading->hidden = !isLoading;
    }
private:
    bool has_metadata = false; // if true, station title goes on top and track name is the big one
    std::string station = "";
};

class InternetRadioMode::StreamingPipeline {
public:
    StreamingPipeline(Platform::AudioRouter * router):
        bufferNetData(256 * 1024),
        bufferPcmData(1024 * 1024),
        queueNetData(bufferNetData),
        queuePcmData(bufferPcmData),
        urlStream(),
        codecAAC(), codecMP3(),
        decoder(&queueNetData, &codecAAC),
        sndTask("IRASND", 8192, 17, 0),
        codecTask("IRADEC", 8192, configMAX_PRIORITIES - 1, 1),
        netTask("IRANET", 8192, 14, 1),
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
    }

    void start(const std::string url, std::function<void(bool)> loadingCallback) {
        running = true;
        loadingCallback(true);
        ESP_LOGI(LOG_TAG, "Streamer is starting");
        queueNetData.begin();
        queuePcmData.begin();
        netTask.begin([this, url, loadingCallback]() { 
                urlStream.setMetadataCallback(_update_meta_global);
                urlStream.begin(url.c_str());
                ESP_LOGI(LOG_TAG, "Streamer did begin URL");
                
                const char * _srv_mime = urlStream.httpRequest().reply().get("Content-Type");
                if(_srv_mime != nullptr) {
                    const std::string srv_mime = _srv_mime;
                    ESP_LOGI(LOG_TAG, "Server reports content-type: %s", srv_mime.c_str());
                    if(srv_mime == "audio/mpeg" || srv_mime == "audio/mp3") {
                        decoder.setDecoder(&codecMP3);
                        decoder.setOutput(queuePcmData);
                        codecMP3.setNotifyActive(true);
                        codecMP3.addNotifyAudioChange(*outPort);
                    }
                    else if(srv_mime == "audio/aac") {
                        decoder.setDecoder(&codecAAC);
                        decoder.setOutput(queuePcmData);
                        codecAAC.setNotifyActive(true);
                        codecAAC.addNotifyAudioChange(*outPort);
                    }
                    else {
                        ESP_LOGE(LOG_TAG, "Content-type is not supported, TODO show error");
                        vTaskDelay(portMAX_DELAY);
                    }
                } else {
                    ESP_LOGE(LOG_TAG, "No content-type reported, TODO show error");
                    vTaskDelay(portMAX_DELAY);
                }
                decoder.setNotifyActive(true);
                decoder.begin();
                ESP_LOGI(LOG_TAG, "Streamer did begin decoder");

                loadingCallback(false);
                TickType_t last_successful_copy = xTaskGetTickCount();
                TickType_t last_stats = xTaskGetTickCount();
                codecTask.begin([this]() { 
                    xSemaphoreTake(semaCodec, portMAX_DELAY);
                    while(bufferNetData.levelPercent() < 20.0f) { 
                        xSemaphoreGive(semaCodec);
                        delay(1);
                        xSemaphoreTake(semaCodec, portMAX_DELAY);
                    }

                    if(!bufferNetData.isEmpty()) {
                        copierDecoding.copy(); 
                    }
                    xSemaphoreGive(semaCodec);
                    delay(1); 
                });
                sndTask.begin([this]() { 
                    xSemaphoreTake(semaSnd, portMAX_DELAY);
                    while(bufferPcmData.levelPercent() < 50.0f) {
                        xSemaphoreGive(semaSnd);
                        delay(1); 
                        xSemaphoreTake(semaSnd, portMAX_DELAY);
                    }
                    xSemaphoreGive(semaSnd);
                    while(1) {
                        xSemaphoreTake(semaSnd, portMAX_DELAY);
                        if(!copierPlaying.copy()) {
                            ESP_LOGE(LOG_TAG, "copierPlaying underrun!? PCM buffer %.00f%%, net buffer %.00f%%", bufferPcmData.levelPercent(), bufferNetData.levelPercent()); 
                            while(bufferPcmData.levelPercent() < 25.0f) { 
                                xSemaphoreGive(semaSnd);
                                delay(1);
                                xSemaphoreTake(semaSnd, portMAX_DELAY);
                            }
                        }
                        xSemaphoreGive(semaSnd);
                        delay(1); 
                    }
                });
                while(1) {
                    xSemaphoreTake(semaNet, portMAX_DELAY);
                    TickType_t now = xTaskGetTickCount();
                    if(bufferNetData.isFull() || copierDownloading.copy()) {
                        last_successful_copy = now;
                    } else if(now - last_successful_copy >= pdTICKS_TO_MS(1000) && bufferNetData.isEmpty()) {
                        ESP_LOGE(LOG_TAG, "streamer is stalled!?");
                        loadingCallback(true);
                        urlStream.end();
                        ESP_LOGE(LOG_TAG, "streamer is trying to begin URL again");
                        urlStream.begin(url.c_str());
                        last_successful_copy = now;
                        loadingCallback(false);
                    }
                    if(now - last_stats >= pdTICKS_TO_MS(1000)) {
                        ESP_LOGI(LOG_TAG, "Stats: PCM buffer %.00f%%, net buffer %.00f%%", bufferPcmData.levelPercent(), bufferNetData.levelPercent()); 
                        last_stats = now;
                    }
                    xSemaphoreGive(semaNet);
                    delay(1);
                }
            }
        );
    }

    void stop() {
        xSemaphoreTake(semaSnd, portMAX_DELAY);
        xSemaphoreTake(semaCodec, portMAX_DELAY);
        xSemaphoreTake(semaNet, portMAX_DELAY);
        running = false;
        sndTask.end();
        codecTask.end();
        netTask.end();
        queuePcmData.end();
        queueNetData.end();
    }

protected:
    bool running = false;
    StreamCopy copierPlaying;
    StreamCopy copierDecoding;
    StreamCopy copierDownloading;
    AACDecoderHelix codecAAC;
    MP3DecoderHelix codecMP3;
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
};

InternetRadioMode::InternetRadioMode(const PlatformSharedResources res):
    streamer(nullptr),
    station_buttons({}), // todo
    chgSource(Button(res.keypad, (1 << 0))),
    moreStations(Button(res.keypad, (1 << 7))),
    Mode(res) {
        _that = this;
        rootView = new InternetRadioView({{0, 0}, {160, 32}});

        for(int i = 1; i <= 6; i++) {
            station_buttons.push_back(Button(res.keypad, (1 << i)));
        }
}

void InternetRadioMode::setup() {
    rootView->reset_meta("Radio");
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
    resources.router->activate_route(Platform::AudioRoute::ROUTE_INTERNAL_CPU);
}

void InternetRadioMode::select_station(int index) {

    // TODO: database
    switch(index) {
        case 0: rootView->reset_meta("Happy Hardcore"); play("http://u1.happyhardcore.com/"); break;
        case 1: rootView->reset_meta("NDR 1"); play("http://icecast.ndr.de/ndr/ndr1wellenord/kiel/mp3/128/stream.mp3"); break;
        case 2: rootView->reset_meta("Радио Проводач"); play("http://station.waveradio.org/provodach"); break;
        case 3: rootView->reset_meta("Проводач (MP3)");play("http://station.waveradio.org/provodach.mp3"); break;
        case 4: rootView->reset_meta("Советская Волна");play("http://station.waveradio.org/soviet"); break;;
        case 5: rootView->reset_meta("Relax FM");play("http://srv01.gpmradio.ru/stream/reg/mp3/128/region_relax_86"); break;

        default: return;
    }
    rootView->channelBar->set_active_ch_idx(index);
}

// cannot use lambda, so have to resort to this hack for now
InternetRadioMode* InternetRadioMode::_that = nullptr;
void InternetRadioMode::_update_meta_global(MetaDataType type, const char * str, int len) {
    ESP_LOGI(LOG_TAG, "META['%s'] = '%s'", toStr(type), str);
    if(_that != nullptr) _that->update_meta(type, str, len); 
}

void InternetRadioMode::update_meta(MetaDataType type, const char * str, int len) {
    rootView->update_meta(type, str, len);
}

void InternetRadioMode::play(const std::string url) {
    rootView->set_loading(true);

    if(streamer != nullptr) {
        ESP_LOGI(LOG_TAG, "Stop old streamer");
        streamer->stop();
        ESP_LOGI(LOG_TAG, "Delete old streamer");
        delete streamer;
        streamer = nullptr;
    }

    ESP_LOGI(LOG_TAG, "Create new streamer");
    streamer = new StreamingPipeline(resources.router);
    ESP_LOGI(LOG_TAG, "Start new streamer");
    streamer->start(
        url,
        [this](bool isLoading) { rootView->set_loading(isLoading); }
    );
}

void InternetRadioMode::loop() {
    for(int i = 0; i < 6; i++) {
        if(station_buttons[i].is_clicked()) {
            select_station(i);
            return;
        }
        else if(station_buttons[i].is_held()) {
            // TODO: settings?
        }
    }
    delay(100);
}

void InternetRadioMode::teardown() {
    if(streamer != nullptr) {
        streamer->stop();
        delete streamer;
    }

    streamer = nullptr;
    resources.router->activate_route(Platform::AudioRoute::ROUTE_NONE_INACTIVE);
}

InternetRadioMode::~InternetRadioMode() {
    _that = nullptr;
    delete rootView;
}

UI::View& InternetRadioMode::main_view() {
    return *rootView;
}