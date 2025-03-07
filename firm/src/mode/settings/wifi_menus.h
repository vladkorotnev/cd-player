#pragma once
#include "settings_icons.h"
#include <localize.h>
#include <menus/framework.h>
#include <esper-core/service.h>

namespace {
    const uint8_t signal_strong_data[] = {
        0x00, 0x01, 0x01, 0x05, 0x05, 0x15, 0x55, 0x55,
    };
    const uint8_t signal_high_data[] = {
        0x00, 0x00, 0x00, 0x04, 0x04, 0x14, 0x54, 0x54,
    };
    const uint8_t signal_mid_data[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x50, 0x50,
    };
    const uint8_t signal_low_data[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40,
    };

    const EGImage signal_strong = {
        .format = EG_FMT_HORIZONTAL,
        .size = {8,8},
        .data = signal_strong_data
    };
    const EGImage signal_high = {
        .format = EG_FMT_HORIZONTAL,
        .size = {8,8},
        .data = signal_high_data
    };
    const EGImage signal_mid = {
        .format = EG_FMT_HORIZONTAL,
        .size = {8,8},
        .data = signal_mid_data
    };
    const EGImage signal_low = {
        .format = EG_FMT_HORIZONTAL,
        .size = {8,8},
        .data = signal_low_data
    };
}
class WiFiPasswordMenuNode: public TextEditor {
public:
    bool enabled;
    WiFiPasswordMenuNode(bool isOpen, std::string& pass_str): enabled(!isOpen), password(pass_str), TextEditor(localized_string("Password")) {}

    void draw_accessory(EGGraphBuf* buf, EGSize bounds) const override { 
        if(enabled) {
            if(password.empty()) {
                UI::ListItem::DisclosureIndicatorDrawingFunc(buf, bounds); 
            }
            else {
                Fonts::EGFont_put_string(Fonts::FallbackWildcard16px, "*****", {bounds.width - 5*8 - 2, 0}, buf);
            }
        } else {
            auto str = localized_string("(None)");
            Fonts::EGFont_put_string(Fonts::FallbackWildcard16px, str.c_str(), {bounds.width - Fonts::EGFont_measure_string(Fonts::FallbackWildcard16px, str.c_str()).width, 0}, buf);
        }
    }

    void execute(MenuNavigator * host) const override {
        if(enabled) {
            host->push(std::make_shared<WiFiPasswordMenuNode>(*this));
        }
    }

    void on_dismissed() override {
        TextEditor::on_dismissed();
        password = edited_string;
    }
private:
    std::string& password;
};

class WiFiNetworkMenuNode : public ListMenuNode {
    public:
        WiFiNetworkMenuNode(const Core::Services::WLAN::ScannedNetwork& network, const std::string& pass, bool isConnected)
            : _network(network), _isConnected(isConnected), password(pass),
            ListMenuNode(network.ssid, nullptr, std::tuple {
                DetailTextMenuNode("SSID", network.ssid),
                WiFiPasswordMenuNode(network.open, password),
                ActionMenuNode("Connect", [this](MenuNavigator * nav) {
                    bool success = Core::Services::WLAN::connect(this->_network.ssid, password);
                    nav->push(std::make_shared<InfoMessageBox>(success ? localized_string("Connected") : localized_string("Connection failed"), [success](MenuNavigator* nav) { 
                        if(success) nav->pop_to_root();
                        else nav->pop();
                    }));
                })
            }) {}
    
        void execute(MenuNavigator* host) const override {
            if(_isConnected) {
                host->pop();
            } else {
                host->push(std::make_shared<WiFiNetworkMenuNode>(*this));
            }
        }
    
        void draw_accessory(EGGraphBuf* buf, EGSize bounds) const override {
            if (_isConnected) {
                EGBlitImage(buf, {bounds.width - icn_checkmark.size.width, bounds.height/2 - icn_checkmark.size.height/2}, &icn_checkmark);
            } else {
                // draw one of the signal icons depending on the RSSI
                const EGImage* signal_icon = &signal_low;
                if (_network.rssi >= -55) {
                    signal_icon = &signal_strong;
                } else if (_network.rssi > -60) {
                    signal_icon = &signal_high;
                } else if (_network.rssi > -70) {
                    signal_icon = &signal_mid;
                }
                EGBlitImage(buf, {bounds.width - signal_icon->size.width - 6, bounds.height/2 - signal_icon->size.height/2}, signal_icon);
            }
    
            MenuNode::draw_accessory(buf, bounds);
        }
    
    private:
        const Core::Services::WLAN::ScannedNetwork _network;
        bool _isConnected;
        std::string password;
    };
    
    class WiFiNetworksListMenuNode : public ListMenuNode {
    public:
        WiFiNetworksListMenuNode():
            spinner(std::make_shared<UI::BigSpinner>(EGRectZero)),
            lblScanning(std::make_shared<UI::Label>(EGRectZero, Fonts::FallbackWildcard16px, UI::Label::Alignment::Center)),
            ListMenuNode("WiFi", &icn_wifi, std::tuple{}) {
                subviews.push_back(lblScanning);
                subviews.push_back(spinner);
        }
        
        void on_presented() override {
            ESP_LOGI("WiFiList", "onPresented");
            set_subnodes({});
    
            scrollBar->hidden = true;
            spinner->hidden = false;
            lblScanning->hidden = false;
            lblScanning->set_value(localized_string("Searching..."));
            spinner->frame = EGRect { {8, 8}, {16, 16} };
            spinner->set_layout();
            lblScanning->frame = EGRect { {spinner->frame.origin.x + spinner->frame.size.width + 6, frame.size.height/2-8}, {frame.size.width - (spinner->frame.origin.x + spinner->frame.size.width + 6), 16} };
    
            update_networks();
            ListMenuNode::on_presented();
        }
    
        void execute(MenuNavigator * host) const override {
            host->push(std::make_shared<WiFiNetworksListMenuNode>(*this));
        }
    
    private:
        std::shared_ptr<UI::BigSpinner> spinner;
        std::shared_ptr<UI::Label> lblScanning;
        
        void update_networks() {
            std::vector<std::shared_ptr<MenuNode>> networkNodes;
            std::vector<Core::Services::WLAN::ScannedNetwork> networks = Core::Services::WLAN::scan_networks();
            
            //find current network in scanned list, if connected
            std::string currentNetwork = Core::Services::WLAN::network_name();
            for (const auto& network : networks) {
                if (network.ssid == currentNetwork) {
                    networkNodes.push_back(std::make_shared<WiFiNetworkMenuNode>(network, Core::Services::WLAN::password(), true));
                    break;
                }
            }
    
            //add remaining networks
            for (const auto& network : networks) {
                if (network.ssid != currentNetwork) {
                  networkNodes.push_back(std::make_shared<WiFiNetworkMenuNode>(network, "", false));
                }
            }
            
            //add empty network list indicator if none were found
            if(networks.size() == 0) {
                lblScanning->set_value(localized_string("No networks"));
                lblScanning->frame = EGRect { {0, frame.size.height/2-8}, {frame.size.width, 16} };
            } else {
                lblScanning->hidden = true;
                scrollBar->hidden = false;
                set_subnodes(networkNodes);
            }
    
            spinner->hidden = true;
        }
    };