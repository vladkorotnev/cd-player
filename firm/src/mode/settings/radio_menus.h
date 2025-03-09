#pragma once
#include <localize.h>
#include <menus/framework.h>
#include <esper-core/service.h>
#include <radio_store.h>
#include "settings_icons.h"

namespace {
    class RadioStationMenuNode : public ListMenuNode {
        public:
            RadioStationMenuNode(int idx):
                index(idx), 
                ListMenuNode("!preset", nullptr, std::tuple {
                    DetailTextMenuNode("Preset", std::to_string(idx + 1)),
                    TextPreferenceEditorNode("Name", NetRadio::NamePrefsKeyForIndex(idx)),
                    TextPreferenceEditorNode("URL", NetRadio::URLPrefsKeyForIndex(idx))
                }) {}
        
            const std::string localized_title() const override {
                const auto name = NetRadio::NameForIndex(index);
                if(!name.empty()) {
                    return std::to_string(index + 1) + ": " + name;
                } else {
                    const std::string format = localized_string("Preset %i");
                    char buffer[format.size() + 2] = { 0 };
                    snprintf(buffer, sizeof(buffer), format.c_str(), index + 1);
                    return std::string(buffer);
                }
            }
        
        protected:
            int index;
        };
}

class RadioStationEditorNode: public ListMenuNode {
public:
    RadioStationEditorNode(): ListMenuNode("Radio Stations", &icn_radio, std::tuple {}) {}

    void execute(MenuNavigator * host) const override {
        host->push(std::make_shared<RadioStationEditorNode>(*this));
    }

    void on_presented() override {
        std::vector<std::shared_ptr<MenuNode>> subnodes = {};
        for(int i = 0; i < 6; i++) {
            subnodes.push_back(std::make_shared<RadioStationMenuNode>(i));
        }
        set_subnodes(subnodes);
        ListMenuNode::on_presented();
    }
};