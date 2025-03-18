#include <mount.h>
#include "consts.h"
#include <LittleFS.h>
#include <esp32-hal-log.h>

static bool fs_mounted = false;
void mount_fs_if_needed() {
    if(!fs_mounted) {
        LittleFS.begin(true, FS_MOUNT_POINT);
        ESP_LOGI("FS", "Free FS size = %i", LittleFS.totalBytes() - LittleFS.usedBytes());
        fs_mounted = true;
    }
}