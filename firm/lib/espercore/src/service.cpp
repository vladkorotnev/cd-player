#include <esper-core/service.h>
#include <esp_sntp.h>
#include <esp32-hal-log.h>
#include <esper-core/prefs.h>
#include <cstring>

static char srv_str[128] = { 0 };

namespace Core::Services {
    void NTP::start() {
        auto srv = Prefs::get(Core::PrefsKey::NTP_SERVER);
        // looks like sntp needs this to be a static string
        strncpy(srv_str, srv.c_str(), 128);
        ESP_LOGI("NTP", "Set server [%s]", srv_str);
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, srv_str);
        sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
        sntp_set_sync_interval(12 * 60 * 60 * 1000);
        sntp_init();
        timeval tv;
        sntp_sync_time(&tv);
        settimeofday(&tv, NULL);
    }
}