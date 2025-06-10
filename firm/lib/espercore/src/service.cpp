#include <esper-core/service.h>
#include <esp_sntp.h>
#include <esp32-hal-log.h>
#include <esper-core/prefs.h>
#include <cstring>
#include <ESPTelnet.h>

static char ntp_server_str[128] = { 0 };
static ESPTelnet * telnet_server = nullptr;

static TaskHandle_t hTelnet = NULL;
static void telnet_task(void*) {
    while(1) {
        telnet_server->loop();
        delay(100);
    }
    vTaskDelete(NULL);
}

// This function will be called by the ESP log library every time ESP_LOG needs to be performed when the telnet server is started.
// Do NOT use the ESP_LOG* macro's in this function ELSE recursive loop and stack overflow! So use printf() instead for debug messages.
int _telnet_log_vprintf(const char *fmt, va_list args) {
    if (telnet_server != nullptr) {
        telnet_server->vprintf(fmt, args);
    }

    return vprintf(fmt, args);
}

namespace Core::Services {
    void NTP::start() {
        auto srv = Prefs::get(Core::PrefsKey::NTP_SERVER);
        // looks like sntp needs this to be a static string
        strncpy(ntp_server_str, srv.c_str(), 128);
        ESP_LOGI("NTP", "Set server [%s]", ntp_server_str);
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, ntp_server_str);
        sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
        sntp_set_sync_interval(12 * 60 * 60 * 1000);
        sntp_init();
        timeval tv;
        sntp_sync_time(&tv);
        settimeofday(&tv, NULL);
    }

    void Telnet::start(int port) {
        static const char LOG_TAG[] = "TELOG";
        if (!Core::Services::WLAN::is_up()) {
            ESP_LOGW(LOG_TAG, "WiFi not ready: not starting!");
            return;
        }
        telnet_server = new ESPTelnet();
        telnet_server->begin(port);
        xTaskCreate(
            telnet_task,
            "TELOG",
            4096,
            NULL,
            0,
            &hTelnet
        );
        esp_log_set_vprintf(_telnet_log_vprintf);
        ESP_LOGI(LOG_TAG, "Telnet logger started");
    }
}