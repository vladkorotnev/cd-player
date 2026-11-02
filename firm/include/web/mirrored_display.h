#pragma once

#include <esper-gui/display.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace Graphics {

/// Decorates any display driver and retains its last complete native framebuffer.
/// The wrapped driver is not owned by this object.
class MirroredDisplayDriver: public Hardware::DisplayDriver {
public:
    explicit MirroredDisplayDriver(Hardware::DisplayDriver& wrapped);
    ~MirroredDisplayDriver() override;

    void initialize() override;
    void set_option(Hardware::DriverOption opt, bool on) override;
    void set_brightness(Hardware::Brightness b) override;
    Hardware::Brightness get_brightness() override;
    const EGSize& get_size() override;
    void transfer(const std::vector<EGRect> changes, const Hardware::BackingBuffer * buffer) override;

    size_t frame_size() const;
    uint32_t version();
    /// Copies a stable snapshot into an already allocated buffer of frame_size() bytes.
    bool snapshot(uint8_t * destination, size_t capacity, uint32_t& version);

private:
    Hardware::DisplayDriver& wrapped;
    SemaphoreHandle_t mutex;
    std::vector<uint8_t> frame;
    uint32_t frame_version = 0;
    bool powered = true;
};

}
