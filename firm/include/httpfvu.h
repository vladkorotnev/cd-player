#pragma once
#include <functional>

namespace HTTPFVU {
    enum HttpFvuStatus {
        HTTP_FVU_IN_PROGRESS,
        HTTP_FVU_SUCCESS,
        HTTP_FVU_ERROR
    };
    typedef std::function<void(HttpFvuStatus, int progress)> HttpFvuCallback;
    bool is_new_version_available();
    void update_file_system(HttpFvuCallback cb);
    void update_firmware(HttpFvuCallback cb);
}