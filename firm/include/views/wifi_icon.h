#include <esper-gui/views/framework.h>

class WiFiIcon: public UI::View {
public:
    WiFiIcon(EGRect f);
    bool needs_display() override;

    /// @brief Set displayed WiFi level
    /// @param level 0: disconnected, 1: low, 2: mid, 3: max
    void set_level(int level);

private:
    std::shared_ptr<UI::ImageView> _img;
    TickType_t last_blink = 0;
    bool blink_phase = false;
    int cur_lvl = 0;
    void set_image(int idx);
};