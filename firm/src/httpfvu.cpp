#include <httpfvu.h>
#include <string>
#include "esp_ota_ops.h"
#include <HTTPClient.h>
#include <esp_log.h>

#define DEST_FS_USES_LITTLEFS
#include <ESP32-targz.h>

#ifndef HTTPFVU_REPO_URL
#define HTTPFVU_REPO_URL "http://esper.genjit.su/fvu/"
#endif

static const char * VER_FURL = (HTTPFVU_REPO_URL "/esper.evr");
static const char * FS_FURL = (HTTPFVU_REPO_URL "/esper-fs.efs");
static const char * FIRM_FURL = (HTTPFVU_REPO_URL "/" FVU_FLAVOR ".efu");

static const char * LOG_TAG = "HTTPFVU";

static HTTPFVU::HttpFvuCallback NO_CALLBACK = [](HTTPFVU::HttpFvuStatus, int){};
static HTTPFVU::HttpFvuCallback _cb = NO_CALLBACK;
static void tarProgressCallback(uint8_t progress) {
    ESP_LOGD(LOG_TAG, "Progress = [%d]", progress);
    _cb(HTTPFVU::HttpFvuStatus::HTTP_FVU_IN_PROGRESS, progress);
}

namespace HTTPFVU {
    std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\n\r");
        if (std::string::npos == first) {
            return "";
        }
        size_t last = str.find_last_not_of(" \t\n\r");
        return str.substr(first, (last - first + 1));
    }

    bool is_new_version_available() {
        const esp_app_desc_t *app_desc = esp_ota_get_app_description();
        if (app_desc == nullptr) {
            ESP_LOGE(LOG_TAG, "Failed to get app description");
            return false;
        }

        const std::string local_ver = app_desc->version;
        ESP_LOGI(LOG_TAG, "Local version: %s", local_ver.c_str());

        HTTPClient http;
        http.begin(VER_FURL);
        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            std::string remote_ver = trim(payload.c_str());
            ESP_LOGI(LOG_TAG, "Remote version: %s", remote_ver.c_str());

            if (remote_ver.empty()) {
                ESP_LOGW(LOG_TAG, "Remote version is empty");
                http.end();
                return false;
            }

            if (local_ver != remote_ver) {
                http.end();
                return true;
            }
        } else {
            ESP_LOGE(LOG_TAG, "HTTP GET VER failed: %d", httpCode);
        }
        http.end();
        return false;
    }

    void update_file_system(HttpFvuCallback cb) {
        HTTPClient http;
        http.begin(FS_FURL);
        int httpCode = http.GET();

        if(httpCode == HTTP_CODE_OK) {
            auto stream = http.getStream();
            TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();

            _cb = cb;
            TARGZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
            TARGZUnpacker->setTarVerify( true ); // true = enables health checks but slows down the overall process
            TARGZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
            TARGZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
            TARGZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
            TARGZUnpacker->setTarProgressCallback(tarProgressCallback);
            TARGZUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
            TARGZUnpacker->setTarMessageCallback( BaseUnpacker::targzPrintLoggerCallback );

            if( !TARGZUnpacker->tarGzStreamExpander( &stream, tarGzFS ) ) {
                ESP_LOGE(LOG_TAG, "tarGzStreamExpander failed with return code #%d\n", TARGZUnpacker->tarGzGetError() );
                cb(HTTP_FVU_ERROR, 100);
            } else {
                cb(HTTP_FVU_SUCCESS, 100);
            }

            delete TARGZUnpacker;
            TARGZUnpacker = nullptr;

            _cb = NO_CALLBACK;
        } else {
            ESP_LOGE(LOG_TAG, "HTTP GET FS failed: %d", httpCode);
            cb(HTTP_FVU_ERROR, 0);
        }

        http.end();
    }

    void update_firmware(HttpFvuCallback cb) {
        HTTPClient http;
        http.begin(FIRM_FURL);
        int httpCode = http.GET();

        if(httpCode == HTTP_CODE_OK) {
            auto stream = http.getStream();
            GzUnpacker  *GZUnpacker = new GzUnpacker();

            _cb = cb;
            GZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
            GZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
            GZUnpacker->setGzProgressCallback( tarProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
            GZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity

            if( !GZUnpacker->gzStreamUpdater( &stream, UPDATE_SIZE_UNKNOWN ) ) {
                ESP_LOGE(LOG_TAG, "tarGzStreamExpander failed with return code #%d\n", GZUnpacker->tarGzGetError() );
                cb(HTTP_FVU_ERROR, 100);
            } else {
                cb(HTTP_FVU_SUCCESS, 100);
            }

            _cb = NO_CALLBACK;
        } else {
            ESP_LOGE(LOG_TAG, "HTTP GET FVU failed: %d", httpCode);
            cb(HTTP_FVU_ERROR, 0);
        }

        http.end();
    }
}
