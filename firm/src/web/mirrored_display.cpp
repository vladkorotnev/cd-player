#include <web/mirrored_display.h>
#include <esper-core/lock.h>
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
        Lock lock(mutex);
        powered = on;
        ++frame_version;
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
        Lock lock(mutex);
        memcpy(frame.data(), buffer->data, frame.size());
        ++frame_version;
    }
    wrapped.transfer(changes, buffer);
}

size_t MirroredDisplayDriver::frame_size() const {
    return frame.size();
}

uint32_t MirroredDisplayDriver::version() {
    Lock lock(mutex);
    return frame_version;
}

bool MirroredDisplayDriver::snapshot(uint8_t * destination, size_t capacity, uint32_t& version) {
    if(destination == nullptr || capacity < frame.size()) return false;
    Lock lock(mutex);
    if(powered) memcpy(destination, frame.data(), frame.size());
    else memset(destination, 0, frame.size());
    version = frame_version;
    return true;
}

}
