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

        // Log to console for TELNET debugging
        const char LOG_TAG[] = "DIAG";
        ESP_LOGI(LOG_TAG, "====== CD DIAGS ======");
        // INFO
        ESP_LOGI(LOG_TAG, "[Info] Model: %s", info->model.empty() ? "(No Model#)" : info->model.c_str());
        ESP_LOGI(LOG_TAG, "[Info] Firmware: %s", info->firmware.empty() ? "(No FW Info)" : info->firmware.c_str());
        ESP_LOGI(LOG_TAG, "[Info] Serial: %s", info->serial.empty() ? "(No Serial#)" : info->serial.c_str());

        // BASIC
        ESP_LOGI(LOG_TAG, "[Basic] ATAPI: %s", diags->is_atapi ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Basic] Self Test Code: %s", selftest_hex);
        ESP_LOGI(LOG_TAG, "[Basic] Media Code: %d", (int)_host->resources.cdrom->check_media());

        // CAPABILITIES
        ESP_LOGI(LOG_TAG, "[Caps] Type: %s", tray_type.c_str());
        ESP_LOGI(LOG_TAG, "[Caps] CD-R: %s", diags->capas.cdr_read ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Caps] CD-RW: %s", diags->capas.cdrw_read ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Caps] Audio Play: %s", diags->capas.audio_play ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Caps] Composite: %s", diags->capas.composite ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Caps] Digital 1: %s", diags->capas.digital1 ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Caps] Digital 2: %s", diags->capas.digital2 ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Caps] CDDA commands: %s", diags->capas.cdda_cmds ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Caps] CDDA accurate: %s", diags->capas.cdda_accurate ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Caps] R-W subcode: %s", diags->capas.rw_subcode_supp ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Caps] R-W deint.: %s", diags->capas.rw_deint_corr ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Caps] P-W lead-in: %s", diags->capas.pw_subcode_leadin ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Caps] C2 pointers: %s", diags->capas.c2_ptrs ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Caps] ISRC read: %s", diags->capas.isrc ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Caps] UPC read: %s", diags->capas.upc ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Caps] Barcode read: %s", diags->capas.barcode ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Caps] Lockable: %s", diags->capas.lock ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Caps] Ejectable: %s", diags->capas.eject ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Caps] Locked: %s", diags->capas.lock_sts ? "Yes" : "No");
        ESP_LOGI(LOG_TAG, "[Caps] Side Change: %s", diags->capas.side_change ? "Yes" : "No");

        // CHANGER
        if(diags->capas.loading_mech == ATAPI::CapabilitiesMechStatusModePage::EjectMecha::MECHTYPE_CHANGER_INDIVIDUAL || diags->capas.loading_mech == ATAPI::CapabilitiesMechStatusModePage::EjectMecha::MECHTYPE_CHANGER_CARTRIDGE || sts->slot_count > 1) {
            ESP_LOGI(LOG_TAG, "[Changer] Slot Presence: %s", diags->capas.chgr_disc_presence ? "Yes" : "No");
            ESP_LOGI(LOG_TAG, "[Changer] Slot Selection: %s", diags->capas.chgr_sss ? "Yes" : "No");
            ESP_LOGI(LOG_TAG, "[Changer] Slot Count: %d", sts->slot_count);
            for(int i = 0; i < sts->slot_count; i++) {
                std::string slot_status = (!sts->changer_slots[i].disc_in && !sts->changer_slots[i].disc_changed) ? "Empty" : ((sts->changer_slots[i].disc_in ? "In" : "") + std::string(" ") + (sts->changer_slots[i].disc_changed ? "Chg" : ""));
                ESP_LOGI(LOG_TAG, "[Changer] Slot #%d: %s", i + 1, slot_status.c_str());
            }
        }

        // QUIRKS
        if(memcmp(&quirks, &empty_quirks, sizeof(ATAPI::Device::Quirks)) != 0) {
            if(quirks.no_media_codes) ESP_LOGI(LOG_TAG, "[Quirks] Bad Media Codes: Yes");
            if(quirks.busy_ass) ESP_LOGI(LOG_TAG, "[Quirks] Sticky BSY bit: Yes");
            if(quirks.must_use_softscan) ESP_LOGI(LOG_TAG, "[Quirks] Simulated SCAN: Yes");
            if(quirks.fucky_toc_reads) ESP_LOGI(LOG_TAG, "[Quirks] Unstable TOC: Yes");
            if(quirks.no_drq_in_toc) ESP_LOGI(LOG_TAG, "[Quirks] Unstable DRQ: Yes");
            if(quirks.alternate_max_speed) ESP_LOGI(LOG_TAG, "[Quirks] Speed Limit: %d", quirks.alternate_max_speed);
        }

        ESP_LOGI(LOG_TAG, "======================");

        ListMenuNode::on_presented();
    }
protected:
    SettingsMenuNavigator * _host;
    std::shared_ptr<UI::BigSpinner> spinner;
};