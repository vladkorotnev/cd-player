#include <esper-gui/views/framework.h>

class WiFiIcon: public UI::View {
public:
    WiFiIcon(EGRect f);
    bool needs_display() override;


private:
    std::shared_ptr<UI::ImageView> _img;
    TickType_t last_blink = 0;
    TickType_t last_check = 0;
    bool blink_phase = false;
    int cur_lvl = 0;
    void set_level(int level);
    void set_image(int idx);
};