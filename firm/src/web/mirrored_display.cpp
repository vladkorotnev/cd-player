#include <web/mirrored_display.h>
#include <cstring>

namespace Graphics {

MirroredDisplayDriver::MirroredDisplayDriver(Hardware::DisplayDriver& driver):
    wrapped(driver),
    mutex(xSemaphoreCreateMutex()),
    frame(driver.get_size().width * ((driver.get_size().height + 7) / 8), 0) {}

MirroredDisplayDriver::~MirroredDisplayDriver() {
    if(mutex != nullptr) vSemaphoreDelete(mutex);
}

void MirroredDisplayDriver::initialize() {
    wrapped.initialize();
}

void MirroredDisplayDriver::set_option(Graphics::Hardware::DriverOption option, bool on) {
    wrapped.set_option(option, on);
    
    if (option == Graphics::Hardware::DriverOption::DISP_OPTION_POWER) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        powered = on;
        ++frame_version;
        xSemaphoreGive(mutex);
    }
}

void MirroredDisplayDriver::set_brightness(Hardware::Brightness b) {
    wrapped.set_brightness(b);
}

Hardware::Brightness MirroredDisplayDriver::get_brightness() {
    return wrapped.get_brightness();
}

const EGSize& MirroredDisplayDriver::get_size() {
    return wrapped.get_size();
}

void MirroredDisplayDriver::transfer(const std::vector<EGRect> changes, const Hardware::BackingBuffer * buffer) {
    if(buffer != nullptr && buffer->fmt == EG_FMT_NATIVE &&
       buffer->size.width == wrapped.get_size().width &&
       buffer->size.height == wrapped.get_size().height && buffer->data != nullptr) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        memcpy(frame.data(), buffer->data, frame.size());
        ++frame_version;
        xSemaphoreGive(mutex);
    }
    wrapped.transfer(changes, buffer);
}

size_t MirroredDisplayDriver::frame_size() const {
    return frame.size();
}

uint32_t MirroredDisplayDriver::version() {
    xSemaphoreTake(mutex, portMAX_DELAY);
    uint32_t version = frame_version;
    xSemaphoreGive(mutex);
    return version;
}

bool MirroredDisplayDriver::snapshot(uint8_t * destination, size_t capacity, uint32_t& version) {
    if(destination == nullptr || capacity < frame.size()) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if(powered) memcpy(destination, frame.data(), frame.size());
    else memset(destination, 0, frame.size());
    version = frame_version;
    xSemaphoreGive(mutex);
    return true;
}

}
