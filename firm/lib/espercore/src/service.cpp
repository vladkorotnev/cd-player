#include <esper-core/service.h>
#include <esp_sntp.h>
#include <esp32-hal-log.h>

namespace Core::Services {
    void NTP::start() {
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, "pool.ntp.org"); // TODO prefs
        sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
        sntp_set_sync_interval(12 * 60 * 60 * 1000);
        sntp_init();
    }
}