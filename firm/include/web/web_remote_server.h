#pragma once

#include <web/web_remote.h>
#include <web/mirrored_display.h>
#include <esp_http_server.h>
#include <freertos/queue.h>
#include <atomic>

class ModeHost;
enum ModeSelection: int;

class WebRemoteServer {
public:
    WebRemoteServer(Platform::WebRemote& remote, Graphics::MirroredDisplayDriver& display, ModeHost& host);
    void begin();

private:
    static void task_entry(void * arg);
    void run();
    bool start_server();
    void stop_server();
    void queue_frame_if_changed();
    void queue_mode_if_changed();

    static esp_err_t status_handler(httpd_req_t * req);
    static esp_err_t display_handler(httpd_req_t * req);
    static esp_err_t preferences_get_handler(httpd_req_t * req);
    static esp_err_t preferences_post_handler(httpd_req_t * req);
    static esp_err_t websocket_handler(httpd_req_t * req);
    static void broadcast_work(void * arg);
    static void broadcast_mode_work(void * arg);
    static void mode_changed(ModeSelection mode, void * context);

    Platform::WebRemote& remote;
    Graphics::MirroredDisplayDriver& display;
    ModeHost& host;
    httpd_handle_t server = nullptr;
    std::atomic<bool> broadcast_queued {false};
    std::atomic<bool> mode_broadcast_queued {false};
    QueueHandle_t mode_changes = nullptr;
    uint32_t sent_version = UINT32_MAX;
    int sent_mode = -1;
};
