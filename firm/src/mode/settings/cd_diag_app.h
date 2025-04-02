#pragma once
#include <modes/settings_mode.h>

class BoolDisplayItem: public DetailTextMenuNode {
public:
    BoolDisplayItem(const std::string name, bool value): DetailTextMenuNode(name, value ? "yes" : "NO") {}
};

class CDDiagsMenuNode : public ListMenuNode {
public:
    CDDiagsMenuNode(SettingsMenuNavigator * nav): 
        _host(nav), 
        spinner(std::make_shared<UI::BigSpinner>(EGRect {{160/2 - 10, 32/2 - 10}, {20, 20}})),
        ListMenuNode("CD Diagnostics", nullptr, std::tuple {})
        {
            subviews.push_back(spinner);
        }

    void on_presented() override {
        auto diags = _host->resources.cdrom->get_diags();
        auto info = _host->resources.cdrom->get_info();
        auto quirks = _host->resources.cdrom->get_quirks();
        auto sts = _host->resources.cdrom->query_state();

        char selftest_hex[6] = { 0 };
        snprintf(selftest_hex, 6, "%04X", diags->self_test_result);

        std::string tray_type;
        switch(diags->capas.loading_mech) {
            case ATAPI::CapabilitiesMechStatusModePage::EjectMecha::MECHTYPE_CADDY:
                tray_type = "Caddy";
                break;

            case ATAPI::CapabilitiesMechStatusModePage::EjectMecha::MECHTYPE_TRAY:
                tray_type = "Tray";
                break;
            
            case ATAPI::CapabilitiesMechStatusModePage::EjectMecha::MECHTYPE_POPUP:
                tray_type = "Popup";
                break;

            case ATAPI::CapabilitiesMechStatusModePage::EjectMecha::MECHTYPE_CHANGER_INDIVIDUAL:
                tray_type = "Changer";
                break;

            case ATAPI::CapabilitiesMechStatusModePage::EjectMecha::MECHTYPE_CHANGER_CARTRIDGE:
                tray_type = "Magazine";
                break;

            default:
                tray_type = "? (" + std::to_string((int) diags->capas.loading_mech) + ")";
                break;
        }

        std::vector<std::shared_ptr<MenuNode>> subnodes = {
            std::make_shared<DetailTextMenuNode>("", "Info"),
            std::make_shared<MenuNode>(info->model.empty() ? "(No Model#)":info->model),
            std::make_shared<MenuNode>(info->firmware.empty() ? "(No FW Info)":info->firmware),
            std::make_shared<MenuNode>(info->serial.empty() ? "(No Serial#)":info->serial),
            std::make_shared<DetailTextMenuNode>("", "Basic"),
            std::make_shared<BoolDisplayItem>("ATAPI", diags->is_atapi),
            std::make_shared<DetailTextMenuNode>("Self Test Code", selftest_hex),
            std::make_shared<DetailTextMenuNode>("Media Code", std::to_string((int) _host->resources.cdrom->check_media())),
            std::make_shared<DetailTextMenuNode>("", "Capabilities"),
            std::make_shared<DetailTextMenuNode>("Type", tray_type),
            std::make_shared<BoolDisplayItem>("CD-R", diags->capas.cdr_read),
            std::make_shared<BoolDisplayItem>("CD-RW", diags->capas.cdrw_read),
            std::make_shared<BoolDisplayItem>("Audio Play", diags->capas.audio_play),
            std::make_shared<BoolDisplayItem>("Composite", diags->capas.composite),
            std::make_shared<BoolDisplayItem>("Digital 1", diags->capas.digital1),
            std::make_shared<BoolDisplayItem>("Digital 2", diags->capas.digital2),
            std::make_shared<BoolDisplayItem>("CDDA commands", diags->capas.cdda_cmds),
            std::make_shared<BoolDisplayItem>("CDDA accurate", diags->capas.cdda_accurate),
            std::make_shared<BoolDisplayItem>("R-W subcode", diags->capas.rw_subcode_supp),
            std::make_shared<BoolDisplayItem>("R-W deint.", diags->capas.rw_deint_corr),
            std::make_shared<BoolDisplayItem>("P-W lead-in", diags->capas.pw_subcode_leadin),
            std::make_shared<BoolDisplayItem>("C2 pointers", diags->capas.c2_ptrs),
            std::make_shared<BoolDisplayItem>("ISRC read", diags->capas.isrc),
            std::make_shared<BoolDisplayItem>("UPC read", diags->capas.upc),
            std::make_shared<BoolDisplayItem>("Barcode read", diags->capas.barcode),
            std::make_shared<BoolDisplayItem>("Lockable", diags->capas.lock),
            std::make_shared<BoolDisplayItem>("Ejectable", diags->capas.eject),
            std::make_shared<BoolDisplayItem>("Locked", diags->capas.lock_sts),
            std::make_shared<BoolDisplayItem>("Side Change", diags->capas.side_change),
        };

        if(diags->capas.loading_mech == ATAPI::CapabilitiesMechStatusModePage::EjectMecha::MECHTYPE_CHANGER_INDIVIDUAL || diags->capas.loading_mech == ATAPI::CapabilitiesMechStatusModePage::EjectMecha::MECHTYPE_CHANGER_CARTRIDGE || sts->slot_count > 1) {
            subnodes.push_back(std::make_shared<BoolDisplayItem>("Slot Presence", diags->capas.chgr_disc_presence));
            subnodes.push_back(std::make_shared<BoolDisplayItem>("Slot Selection", diags->capas.chgr_sss));
            subnodes.push_back(std::make_shared<DetailTextMenuNode>("Slot Count", std::to_string(sts->slot_count)));
            for(int i = 0; i < sts->slot_count; i++) {
                subnodes.push_back(std::make_shared<DetailTextMenuNode>("Slot #" + std::to_string(i + 1), (
                    (!sts->changer_slots[i].disc_in && !sts->changer_slots[i].disc_changed) ? "Empty" : ((sts->changer_slots[i].disc_in ? "In" : "") + std::string(" ") + (sts->changer_slots[i].disc_changed ? "Chg" : ""))
                )));
            }
        }

        static const ATAPI::Device::Quirks empty_quirks = {0};
        if(memcmp(&quirks, &empty_quirks, sizeof(ATAPI::Device::Quirks)) != 0) {
            subnodes.push_back(std::make_shared<DetailTextMenuNode>("", "Quirks"));

            if(quirks.no_media_codes) subnodes.push_back(std::make_shared<MenuNode>("Bad Media Codes"));
            if(quirks.busy_ass) subnodes.push_back(std::make_shared<MenuNode>("Sticky BSY bit"));
            if(quirks.must_use_softscan) subnodes.push_back(std::make_shared<MenuNode>("Simulated SCAN"));
            if(quirks.fucky_toc_reads) subnodes.push_back(std::make_shared<MenuNode>("Unstable TOC"));
            if(quirks.no_drq_in_toc) subnodes.push_back(std::make_shared<MenuNode>("Unstable DRQ"));
            if(quirks.alternate_max_speed) subnodes.push_back(std::make_shared<DetailTextMenuNode>("Speed Limit", std::to_string(quirks.alternate_max_speed)));
        }

        set_subnodes(subnodes);
        spinner->hidden = true;

        ListMenuNode::on_presented();
    }
protected:
    SettingsMenuNavigator * _host;
    std::shared_ptr<UI::BigSpinner> spinner;
};