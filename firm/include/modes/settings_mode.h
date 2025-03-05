#pragma once
#include <mode.h>
#include <esper-gui/views/framework.h>
#include <stack>

namespace {
    // By PiiXL
    // https://piiixl.itch.io/mega-1-bit-icons-bundle
    const uint8_t icn_wifi_data[] = {
        0x3e, 0x0c, 0x61, 0x1e, 0x5f, 0xde, 0x40, 0x0c, 0x40, 0x00, 0x41, 0x48, 0x22, 0xa8, 0x51, 0x74, 
        0x6a, 0xf6, 0x37, 0xf6, 0x1b, 0xf6, 0x4d, 0xf6, 0x66, 0xfc, 0x70, 0x00, 0x58, 0x00, 0xff, 0xc0
    };
    const uint8_t icn_cd_data[] = {
        0xff, 0xff, 0xff, 0xff, 0xd7, 0xff, 0xff, 0xff, 0xd7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
        0x00, 0x00, 0xff, 0xff, 0x80, 0x01, 0xa2, 0x45, 0xa1, 0x85, 0x10, 0x08, 0x08, 0x10, 0x07, 0xe0,         
    };
    const uint8_t icn_radio_data[] = {
        0x00, 0x1c, 0x00, 0xe0, 0x07, 0x00, 0x00, 0x00, 0xdf, 0xff, 0xd0, 0x01, 0xd5, 0x55, 0xd1, 0x01, 
        0xdf, 0xff, 0xda, 0xff, 0xd5, 0x49, 0xda, 0xc9, 0xd5, 0x7f, 0xda, 0xff, 0xdf, 0xff, 0xdf, 0xff, 
    };
    const uint8_t icn_bt_data[] = {
        0x00, 0x00, 0x0b, 0xf8, 0x14, 0x04, 0x15, 0x54, 0x14, 0x04, 0x15, 0x54, 0x14, 0x04, 0x15, 0x14, 
        0x14, 0x34, 0x14, 0x74, 0x14, 0xf4, 0x15, 0xf4, 0x14, 0x04, 0x17, 0xfc, 0x17, 0xbc, 0x0b, 0xf8, 
    };
    const uint8_t icn_sys_data[] = {
        0x00, 0x00, 0x03, 0xe0, 0x07, 0xc0, 0x0f, 0x80, 0x0f, 0x00, 0x0f, 0x00, 0x0f, 0x01, 0x0f, 0x83, 
        0x0f, 0xc7, 0x1f, 0xff, 0x3f, 0xff, 0x6f, 0xfe, 0x5f, 0xfc, 0x3b, 0x00, 0x76, 0x00, 0x6c, 0x00
    };
    const uint8_t icn_about_data[] = {
        0x00, 0x00, 0x0f, 0xf0, 0x11, 0xf8, 0x3e, 0xfc, 0x7b, 0xc2, 0x7b, 0xfe, 0x7b, 0xce, 0x7f, 0xce, 
        0x7f, 0xfe, 0x6e, 0x3e, 0x57, 0xfe, 0x50, 0x3e, 0x3f, 0xdc, 0x7c, 0x38, 0x7d, 0xf0, 0x3c, 0x00
    };
    
    const UI::Image icn_wifi = {
        .format = EG_FMT_HORIZONTAL,
        .size = {16, 16},
        .data = icn_wifi_data
    };
    const UI::Image icn_cd = {
        .format = EG_FMT_HORIZONTAL,
        .size = {16, 16},
        .data = icn_cd_data
    };
    const UI::Image icn_radio = {
        .format = EG_FMT_HORIZONTAL,
        .size = {16, 16},
        .data = icn_radio_data
    };
    const UI::Image icn_bt = {
        .format = EG_FMT_HORIZONTAL,
        .size = {16, 16},
        .data = icn_bt_data
    };
    const UI::Image icn_sys = {
        .format = EG_FMT_HORIZONTAL,
        .size = {16, 16},
        .data = icn_sys_data
    };
    const UI::Image icn_about = {
        .format = EG_FMT_HORIZONTAL,
        .size = {16, 16},
        .data = icn_about_data
    };
}

class MenuNavigator;
class MenuPresentable: public virtual UI::View {
public:
    virtual void on_presented() {}
    virtual void on_dismissed() {}
    virtual void on_key_pressed(VirtualKey, MenuNavigator*) {}
};

class MenuNavigator: public UI::View {
public:
    MenuNavigator(std::shared_ptr<MenuPresentable> root, EGRect f): back_stack{}, current{root}, UI::View(f) {
        set_current(root);
    }

    void push(std::shared_ptr<MenuPresentable> next_view) {
        back_stack.push(current);
        current->on_dismissed();
        set_current(next_view);
    }

    void pop() {
        if(back_stack.empty()) return;
        set_current(back_stack.top());
        back_stack.pop();
    }

    void on_key_pressed(VirtualKey k) { current->on_key_pressed(k, this); }

private:
    std::shared_ptr<MenuPresentable> current;

    void set_current(std::shared_ptr<MenuPresentable> cur) {
        subviews.clear();
        cur->frame = {EGPointZero, {frame.size.width, frame.size.height}};
        ESP_LOGI("MenuNav", "Cur new w=%i h=%i", cur->frame.size.width, cur->frame.size.height);
        subviews.push_back(cur);
        current = cur;
        current->on_presented();
    }
    std::stack<std::shared_ptr<MenuPresentable>> back_stack;
};

class MenuNode {
public:
    MenuNode(const std::string t, const UI::Image* img): title{t}, icon{img} {}
    virtual ~MenuNode() = default;

    const std::string title;
    const UI::Image * icon;
    virtual void execute(MenuNavigator * host) const { ESP_LOGE("MenuNode", "Did you forget to override execute()?"); }
    virtual bool is_submenu() const { return false; }
};

class ListMenuNode: public MenuPresentable, public MenuNode,  public UI::ListView {
public:
    ListMenuNode(const std::string title, const UI::Image* icon, const std::vector<std::shared_ptr<MenuNode>>& items): 
        subnodes(items),
        UI::ListView(EGRectZero, {}), MenuNode(title, icon) {
    }

    ListMenuNode(const std::string title, const std::vector<std::shared_ptr<MenuNode>>& items): 
        ListMenuNode(title, nullptr, items) {}

    bool is_submenu() const override { return true; }

    void execute(MenuNavigator * host) const override {
        host->push(std::make_shared<ListMenuNode>(*this));
    }

    void on_presented() override {
        // update frames of things after push has set our frame correctly
        contentView->frame = EGRect {EGPointZero, {frame.size.width - 6, 0}};
        scrollBar->frame = EGRect {{frame.size.width - 5, 0}, {5, frame.size.height}};
        std::vector<std::shared_ptr<UI::ListItem>> viewItems = {};
        for(auto& subnode: subnodes) {
            viewItems.push_back(std::make_shared<UI::ListItem>(subnode->title, subnode->is_submenu(), subnode->icon));
        }
        set_items(viewItems);
    }

    void on_key_pressed(VirtualKey k, MenuNavigator* host) override {
        if(k == RVK_CURS_DOWN) down();
        else if(k == RVK_CURS_UP) up();
        else if(k == RVK_CURS_ENTER || (k == RVK_CURS_RIGHT && subnodes[selection]->is_submenu())) {
            subnodes[selection]->execute(host);
        }
        else if(k == RVK_CURS_LEFT) {
            host->pop();
        }
    }
protected:
    const std::vector<std::shared_ptr<MenuNode>> subnodes;
};

class SettingsMode: public Mode {
    public:
        SettingsMode(const PlatformSharedResources res, ModeHost * host): 
            rootView(std::make_shared<ListMenuNode>(
                ListMenuNode(
                    "Settings",
                    {
                        std::make_shared<MenuNode>("Item 1", &icn_wifi),
                        std::make_shared<ListMenuNode>(ListMenuNode("Drill Down", &icn_about, {
                            std::make_shared<MenuNode>("Drill Item 1", &icn_cd),
                            std::make_shared<MenuNode>("Drill Item 2", &icn_bt),
                            std::make_shared<MenuNode>("Drill Item 3", &icn_radio),
                        }))
                    }
                )
            ), {EGPointZero, {160, 32}}),
            Mode(res, host) {
        }
    
        void setup() override {
        }

        void loop() override {
            delay(100);
        }

        void on_remote_key_pressed(VirtualKey key) override {
            rootView.on_key_pressed(key);
        }

        void teardown() override {
        }
    
        UI::View& main_view() override {
            return rootView;
        }
    
    private:
        MenuNavigator rootView;
        const char * LOG_TAG = "SETTING";
};