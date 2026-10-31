#pragma once

#include <esper-core/remote.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace Platform {

/// Merges web key presses with an existing remote. The wrapped remote is not owned.
class WebRemote: public Remote {
public:
    explicit WebRemote(Remote& wrapped);
    ~WebRemote() override;

    void update() override;
    bool has_new_code() override;
    RemoteCode code() override;

    /// Called by the web server task. Returns false for an invalid key or full queue.
    bool submit_key(RemoteCode key);

private:
    Remote& wrapped;
    QueueHandle_t pending;
    RemoteCode current = 0;
};

}
