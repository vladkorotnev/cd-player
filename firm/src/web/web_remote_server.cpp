#include <web/web_remote_server.h>
#include <mode_host.h>
#include <shared_prefs.h>
#include <web/web_preferences.h>
#include <esper-core/wlan.h>
#include <cJSON.h>
#include <esp32-hal-log.h>
#include <freertos/task.h>
#include <cstring>
#include <string>
#include <vector>

namespace {
const char * LOG_TAG = "WEB_REMOTE";
constexpr size_t MAX_BODY = 1024;
constexpr size_t MAX_WS_MESSAGE = 128;

struct FrameJob {
    WebRemoteServer * owner;
    httpd_handle_t server;
    std::vector<uint8_t> bytes;
};

struct ModeJob {
    WebRemoteServer * owner;
    httpd_handle_t server;
    int mode;
};

void no_cache(httpd_req_t * req) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

esp_err_t json_response(httpd_req_t * req, cJSON * json) {
    if(json == nullptr) return httpd_resp_send_500(req);
    char * body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if(body == nullptr) return httpd_resp_send_500(req);
    no_cache(req);
    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    cJSON_free(body);
    return result;
}

esp_err_t bad_request(httpd_req_t * req, const char * message) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, message);
}

bool same_origin(httpd_req_t * req) {
    char origin[128] = {};
    if(httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK) return true;
    char host[128] = {};
    if(httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) return false;
    std::string expected = std::string("http://") + host;
    return expected == origin;
}

esp_err_t send_display_frame(httpd_req_t * req, Graphics::MirroredDisplayDriver& display) {
    std::vector<uint8_t> bytes(display.frame_size());
    uint32_t version = 0;
    if(!display.snapshot(bytes.data(), bytes.size(), version)) return ESP_FAIL;
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_BINARY;
    frame.payload = bytes.data();
    frame.len = bytes.size();
    return httpd_ws_send_frame(req, &frame);
}

esp_err_t send_mode_frame(httpd_req_t * req, int mode) {
    char payload[48] = {};
    int length = snprintf(payload, sizeof(payload), "{\"type\":\"mode\",\"mode\":%d}", mode);
    if(length < 0 || length >= (int)sizeof(payload)) return ESP_FAIL;
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = (uint8_t *)payload;
    frame.len = length;
    return httpd_ws_send_frame(req, &frame);
}

void broadcast_frame(httpd_handle_t server, httpd_ws_frame_t& frame) {
    int clients[4] = {};
    size_t count = 4;
    if(httpd_get_client_list(server, &count, clients) != ESP_OK) return;
    for(size_t i = 0; i < count; ++i) {
        if(httpd_ws_get_fd_info(server, clients[i]) == HTTPD_WS_CLIENT_WEBSOCKET)
            httpd_ws_send_frame_async(server, clients[i], &frame);
    }
}
}

WebRemoteServer::WebRemoteServer(Platform::WebRemote& input,
    Graphics::MirroredDisplayDriver& output, ModeHost& mode_host):
    remote(input), display(output), host(mode_host),
    mode_changes(xQueueCreate(8, sizeof(int))) {}

void WebRemoteServer::begin() {
    if(mode_changes == nullptr) ESP_LOGE(LOG_TAG, "Mode event queue allocation failed");
    else host.set_mode_changed_callback(mode_changed, this);
    xTaskCreate(task_entry, "WEBREMOTE", 4096, this, 1, nullptr);
}

void WebRemoteServer::mode_changed(ModeSelection mode, void * context) {
    auto * self = static_cast<WebRemoteServer *>(context);
    int value = (int)mode;
    if(xQueueSend(self->mode_changes, &value, 0) != pdTRUE) {
        int discarded = 0;
        xQueueReceive(self->mode_changes, &discarded, 0);
        xQueueSend(self->mode_changes, &value, 0);
    }
}

void WebRemoteServer::task_entry(void * arg) {
    static_cast<WebRemoteServer *>(arg)->run();
}

void WebRemoteServer::run() {
    TickType_t last_lifecycle_check = xTaskGetTickCount() - pdMS_TO_TICKS(250);
    for(;;) {
        TickType_t now = xTaskGetTickCount();
        if(now - last_lifecycle_check >= pdMS_TO_TICKS(250)) {
            bool wanted = Prefs::get(PREFS_KEY_WEB_REMOTE_ENABLED) && Core::Services::WLAN::is_up();
            if(wanted && server == nullptr) start_server();
            else if(!wanted && server != nullptr) stop_server();
            last_lifecycle_check = now;
        }
        if(server != nullptr) {
            queue_mode_if_changed();
            queue_frame_if_changed();
        }
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

bool WebRemoteServer::start_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 4;
    config.stack_size = 5120;
    config.lru_purge_enable = true;
    if(httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(LOG_TAG, "HTTP server start failed");
        server = nullptr;
        return false;
    }

    const httpd_uri_t endpoints[] = {
        {.uri="/api/status", .method=HTTP_GET, .handler=status_handler, .user_ctx=this},
        {.uri="/api/display", .method=HTTP_GET, .handler=display_handler, .user_ctx=this},
        {.uri="/api/preferences", .method=HTTP_GET, .handler=preferences_get_handler, .user_ctx=this},
        {.uri="/api/preferences", .method=HTTP_POST, .handler=preferences_post_handler, .user_ctx=this},
        {.uri="/ws", .method=HTTP_GET, .handler=websocket_handler, .user_ctx=this, .is_websocket=true},
    };
    for(const auto& endpoint: endpoints) {
        if(httpd_register_uri_handler(server, &endpoint) != ESP_OK) {
            ESP_LOGE(LOG_TAG, "Failed to register %s", endpoint.uri);
            stop_server();
            return false;
        }
    }
    sent_version = UINT32_MAX;
    sent_mode = -1;
    if(mode_changes != nullptr) xQueueReset(mode_changes);
    ESP_LOGI(LOG_TAG, "Listening on %s", Core::Services::WLAN::current_ip().c_str());
    return true;
}

void WebRemoteServer::stop_server() {
    if(server == nullptr) return;
    // Let the one outstanding send finish before disposing of its server handle.
    while(broadcast_queued.load() || mode_broadcast_queued.load()) vTaskDelay(pdMS_TO_TICKS(10));
    httpd_stop(server);
    server = nullptr;
    sent_version = UINT32_MAX;
    sent_mode = -1;
}

void WebRemoteServer::queue_frame_if_changed() {
    if(broadcast_queued.load() || display.version() == sent_version) return;
    auto * job = new FrameJob {this, server, std::vector<uint8_t>(display.frame_size())};
    uint32_t version = 0;
    if(!display.snapshot(job->bytes.data(), job->bytes.size(), version) || version == sent_version) {
        delete job;
        return;
    }
    broadcast_queued.store(true);
    if(httpd_queue_work(server, broadcast_work, job) != ESP_OK) {
        broadcast_queued.store(false);
        delete job;
        return;
    }
    sent_version = version;
}

void WebRemoteServer::broadcast_work(void * arg) {
    auto * job = static_cast<FrameJob *>(arg);
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_BINARY;
    frame.payload = job->bytes.data();
    frame.len = job->bytes.size();
    broadcast_frame(job->server, frame);
    job->owner->broadcast_queued.store(false);
    delete job;
}

void WebRemoteServer::queue_mode_if_changed() {
    if(mode_broadcast_queued.load()) return;
    int current = 0;
    bool from_queue = mode_changes != nullptr && xQueueReceive(mode_changes, &current, 0) == pdTRUE;
    if(!from_queue) current = (int)host.get_current_mode();
    if(current == sent_mode) return;
    auto * job = new ModeJob {this, server, current};
    mode_broadcast_queued.store(true);
    if(httpd_queue_work(server, broadcast_mode_work, job) != ESP_OK) {
        mode_broadcast_queued.store(false);
        if(from_queue) xQueueSendToFront(mode_changes, &current, 0);
        delete job;
        return;
    }
    sent_mode = current;
}

void WebRemoteServer::broadcast_mode_work(void * arg) {
    auto * job = static_cast<ModeJob *>(arg);
    char payload[48] = {};
    int length = snprintf(payload, sizeof(payload), "{\"type\":\"mode\",\"mode\":%d}", job->mode);
    if(length >= 0 && length < (int)sizeof(payload)) {
        httpd_ws_frame_t frame = {};
        frame.type = HTTPD_WS_TYPE_TEXT;
        frame.payload = (uint8_t *)payload;
        frame.len = length;
        broadcast_frame(job->server, frame);
    }
    job->owner->mode_broadcast_queued.store(false);
    delete job;
}

esp_err_t WebRemoteServer::status_handler(httpd_req_t * req) {
    auto * self = static_cast<WebRemoteServer *>(req->user_ctx);
    const EGSize& size = self->display.get_size();
    cJSON * status = cJSON_CreateObject();
    cJSON_AddNumberToObject(status, "width", size.width);
    cJSON_AddNumberToObject(status, "height", size.height);
    cJSON_AddStringToObject(status, "format", "vertical-1bpp-msb");
    cJSON_AddStringToObject(status, "websocket", "/ws");
    cJSON_AddNumberToObject(status, "mode", (int)self->host.get_current_mode());
    return json_response(req, status);
}

esp_err_t WebRemoteServer::display_handler(httpd_req_t * req) {
    auto * self = static_cast<WebRemoteServer *>(req->user_ctx);
    std::vector<uint8_t> bytes(self->display.frame_size());
    uint32_t version = 0;
    if(!self->display.snapshot(bytes.data(), bytes.size(), version)) return httpd_resp_send_500(req);
    no_cache(req);
    httpd_resp_set_type(req, "application/octet-stream");
    return httpd_resp_send(req, (const char *)bytes.data(), bytes.size());
}

esp_err_t WebRemoteServer::preferences_get_handler(httpd_req_t * req) {
    if(strchr(req->uri, '?') == nullptr)
        return json_response(req, WebPreferences::read_all());
    char query[64] = {};
    if(httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
        return bad_request(req, "Invalid query string");
    char key[32] = {};
    if(httpd_query_key_value(query, "key", key, sizeof(key)) != ESP_OK)
        return bad_request(req, "Expected key query parameter");
    cJSON * preference = WebPreferences::read_one(key);
    if(preference == nullptr) return httpd_resp_send_404(req);
    return json_response(req, preference);
}

esp_err_t WebRemoteServer::preferences_post_handler(httpd_req_t * req) {
    if(!same_origin(req)) return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Origin rejected");
    char content_type[64] = {};
    if(httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
       strncmp(content_type, "application/json", strlen("application/json")) != 0)
        return bad_request(req, "Expected application/json");
    if(req->content_len == 0 || req->content_len > MAX_BODY) return bad_request(req, "Invalid body length");
    char body[MAX_BODY + 1] = {};
    size_t received = 0;
    while(received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if(n <= 0) return ESP_FAIL;
        received += n;
    }
    cJSON * request = cJSON_Parse(body);
    if(request == nullptr) return bad_request(req, "Invalid JSON");
    const cJSON * key = cJSON_GetObjectItemCaseSensitive(request, "key");
    const cJSON * value = cJSON_GetObjectItemCaseSensitive(request, "value");
    if(!cJSON_IsString(key) || value == nullptr) {
        cJSON_Delete(request);
        return bad_request(req, "Expected key and value");
    }
    std::string key_name = key->valuestring;
    auto result = WebPreferences::write_one(key_name, value);
    cJSON_Delete(request);
    if(result == WebPreferences::WriteResult::unknown_key) return httpd_resp_send_404(req);
    if(result == WebPreferences::WriteResult::invalid_value) return bad_request(req, "Invalid value or type");
    return json_response(req, WebPreferences::read_one(key_name));
}

esp_err_t WebRemoteServer::websocket_handler(httpd_req_t * req) {
    if(!same_origin(req)) return ESP_FAIL;
    auto * self = static_cast<WebRemoteServer *>(req->user_ctx);
    // The IDF server has completed the handshake before invoking this handler.
    if(req->method == HTTP_GET) {
        esp_err_t result = send_mode_frame(req, (int)self->host.get_current_mode());
        return result == ESP_OK ? send_display_frame(req, self->display) : result;
    }

    httpd_ws_frame_t frame = {};
    if(httpd_ws_recv_frame(req, &frame, 0) != ESP_OK) return ESP_FAIL;
    if(frame.type != HTTPD_WS_TYPE_TEXT || frame.len > MAX_WS_MESSAGE) return ESP_FAIL;
    uint8_t body[MAX_WS_MESSAGE + 1] = {};
    frame.payload = body;
    if(httpd_ws_recv_frame(req, &frame, MAX_WS_MESSAGE) != ESP_OK) return ESP_FAIL;

    cJSON * message = cJSON_Parse((const char *)body);
    if(message == nullptr) return ESP_OK;
    const cJSON * type = cJSON_GetObjectItemCaseSensitive(message, "type");
    if(cJSON_IsString(type) && strcmp(type->valuestring, "sync") == 0) {
        send_display_frame(req, self->display);
    } else if(cJSON_IsString(type) && strcmp(type->valuestring, "key") == 0) {
        const cJSON * key = cJSON_GetObjectItemCaseSensitive(message, "key");
        if(cJSON_IsNumber(key) && key->valuedouble == key->valueint)
            self->remote.submit_key(key->valueint);
    }
    cJSON_Delete(message);
    return ESP_OK;
}
