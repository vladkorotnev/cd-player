#include <web/web_remote.h>

namespace Platform {

WebRemote::WebRemote(Remote& remote): wrapped(remote), pending(xQueueCreate(8, sizeof(RemoteCode))) {}

WebRemote::~WebRemote() {
    if(pending != nullptr) vQueueDelete(pending);
}

void WebRemote::update() {
    wrapped.update();
}

bool WebRemote::has_new_code() {
    // Give a physical button press priority without discarding pending web keys.
    if(wrapped.has_new_code()) {
        current = wrapped.code();
        return true;
    }
    return pending != nullptr && xQueueReceive(pending, &current, 0) == pdTRUE;
}

Remote::RemoteCode WebRemote::code() {
    return current;
}

bool WebRemote::submit_key(RemoteCode key) {
    if(key < RVK_START_OF_NUMBERS || key > RVK_CURS_ENTER || pending == nullptr) return false;
    return xQueueSend(pending, &key, 0) == pdTRUE;
}

}
