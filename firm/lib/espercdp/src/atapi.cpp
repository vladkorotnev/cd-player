#include <esper-cdp/atapi.h>
#include <esper-cdp/atapi-protocol.h>
#include <cassert>
#include <algorithm>
#include <cctype>
#include <endian.h>

static const char LOG_TAG[] = "ATAPI";

namespace ATAPI {
    static std::string drive_string(const char * bytes, size_t length, bool swap_words = false) {
        std::string value(bytes, length);
        if (swap_words) {
            for (size_t i = 0; i < length; i += 2) {
                std::swap(value[i], value[i + 1]);
            }
        }
        while (!value.empty()) {
            const auto last = static_cast<unsigned char>(value.back());
            if (std::isprint(last) && !std::isspace(last)) break;
            value.pop_back();
        }
        value.resize(strlen(value.c_str()));
        return value;
    }

    HardwareATAPIDevice::HardwareATAPIDevice(Platform::IDEBus * bus):
        ide(bus),
        Device()
    {
        // compile time sanity checks
        const StatusRegister tmp = StatusRegister { .value = (1 << 7) };
        assert(tmp.BSY);

        semaphore = xSemaphoreCreateBinary();
        xSemaphoreGive(semaphore);

        quirks = {0};
    }

    void HardwareATAPIDevice::reset() {
        initialize();
    }

    bool HardwareATAPIDevice::initialize() {
        xSemaphoreTake(semaphore, portMAX_DELAY);
        ESP_LOGI(LOG_TAG, "Reset");
        info = {};
        _diags = {};
        quirks = {};
        packet_size = 12;
        ide->reset();
        const bool reset_complete = wait_not_busy("reset");
        xSemaphoreGive(semaphore);

        if (!reset_complete) return false;
        _diags.is_atapi = check_atapi_compatible();
        if (!_diags.is_atapi) {
            ESP_LOGE(LOG_TAG, "Expected ATAPI signature 0xeb14");
            return false;
        }

        if (!self_test()) ESP_LOGW(LOG_TAG, "Drive self-test did not report success");
        if (!init_task_file() || !identify()) return false;
        log_inquiry();
        query_state();
        _diags.capas = mode_sense_capabilities();
        set_speed();
        ESP_LOGI(LOG_TAG, "end of Reset");
        return true;
    }

    const DriveInfo * HardwareATAPIDevice::get_info() { return &info; }

    const MechInfo * HardwareATAPIDevice::query_state() {
        Responses::MechanismStatusHeader hdr;
        memset(&hdr, 0, sizeof(hdr));

        const Requests::MechanismStatus req = {
            .opcode = OperationCodes::MECHANISM_STATUS,
            .allocation_length = htobe16( sizeof(hdr) + sizeof(Responses::MechanismStatusSlotTable) * MAX_CHANGER_SLOTS ) // <- oughtta be enough
        };

        xSemaphoreTake(semaphore, portMAX_DELAY);

        send_packet(&req, sizeof(req));
        read_response(&hdr, sizeof(hdr), false);

        hdr.slot_tbl_len = be16toh(hdr.slot_tbl_len);

        ESP_LOGV(LOG_TAG, "Slot = %i, ChgSts = %i, Fault = %i, Door = %i, MechSts = %i, NumSlots = %i, SlotTblLen = %i, LBAPos = %02x%02x%02x", hdr.current_slot, hdr.changer_state, hdr.fault, hdr.door_open, hdr.mechanism_state, hdr.num_slots, hdr.slot_tbl_len, hdr.current_lba[0], hdr.current_lba[1], hdr.current_lba[2]);

        mech_sts.slot_count = std::max((uint8_t)1, (uint8_t)hdr.num_slots);
        mech_sts.is_door_open = hdr.door_open;
        mech_sts.is_playing = (hdr.mechanism_state == Responses::MechanismStatusHeader::MechState::Audio);
        mech_sts.is_fault = hdr.fault;
        mech_sts.current_disc = hdr.current_slot;
        if(hdr.num_slots > 1) {
            switch(hdr.changer_state) {
                case Responses::MechanismStatusHeader::ChangerState::Ready:
                    mech_sts.changer_state = MechInfo::ChangerState::Idle;
                    break;

                case Responses::MechanismStatusHeader::ChangerState::Initializing:
                    mech_sts.changer_state = MechInfo::ChangerState::Preparing;
                    break;

                case Responses::MechanismStatusHeader::ChangerState::Loading:
                case Responses::MechanismStatusHeader::ChangerState::Unloading:
                    mech_sts.changer_state = MechInfo::ChangerState::Changing_Disc;
                    break;
            }
        }
        
        mech_sts.is_in_motion = (hdr.changer_state != Responses::MechanismStatusHeader::ChangerState::Ready);

        if(hdr.num_slots > 1 && hdr.slot_tbl_len > 0) {
            hdr.num_slots = std::min((int)hdr.num_slots, MAX_CHANGER_SLOTS);
            Responses::MechanismStatusSlotTable slot_info = { 0 };
            
            for(int i = 0; i < hdr.num_slots; i++) {
                read_response(&slot_info, sizeof(slot_info), (i == hdr.num_slots - 1));

                ESP_LOGD(LOG_TAG, " - SLOT#%i: Present = %i, Changed = %i", i, slot_info.disc_present, slot_info.disc_changed);
                mech_sts.changer_slots[i].disc_changed = slot_info.disc_changed;
                mech_sts.changer_slots[i].disc_in = slot_info.disc_present;
            }
        }

        read_response(nullptr, 0, true);

        xSemaphoreGive(semaphore);
        return &mech_sts;
    }

    bool HardwareATAPIDevice::check_atapi_compatible() {
        xSemaphoreTake(semaphore, portMAX_DELAY);
        const uint8_t low = ide->read(IDE::Register::CylinderLow).low;
        const uint8_t high = ide->read(IDE::Register::CylinderHigh).low;
        xSemaphoreGive(semaphore);

        ESP_LOGI(LOG_TAG, "Drive signature = 0x%02x%02x", high, low);
        return low == 0x14 && high == 0xEB;
    }

    bool HardwareATAPIDevice::init_task_file() {
        xSemaphoreTake(semaphore, portMAX_DELAY);
        ESP_LOGI(LOG_TAG, "init task file");
        ide->write(IDE::Register::Feature, {{ .low = (FeatureRegister {{ .DMA = false, .overlap = false }}).value, .high = 0xFF }});

        // PIO buffer = 0x200
        ide->write(IDE::Register::CylinderHigh, {{ .low = 0x02, .high = 0xFF }});
        ide->write(IDE::Register::CylinderLow, {{ .low = 0x00, .high = 0xFF }});

        // Disable interrupts
        ide->write(IDE::Register::DeviceControl, {{ .low = (DeviceControlRegister {{ .nIEN = true }}).value, .high = 0xFF }});

        const bool idle = wait_sts_bit_clr({{.DRQ = true, .BSY = true}}, "task file");
        xSemaphoreGive(semaphore);
        return idle;
    }

    bool HardwareATAPIDevice::self_test() {
        xSemaphoreTake(semaphore, portMAX_DELAY);
        ESP_LOGI(LOG_TAG, "Self-test");
        ide->write(IDE::Register::Command, {{ .low = Command::EXECUTE_DEVICE_DIAGNOSTIC, .high = 0xFF }});
        if (!wait_not_busy("self-test")) {
            xSemaphoreGive(semaphore);
            return false;
        }
        data16 rslt = ide->read(IDE::Register::Error);
        ESP_LOGI(LOG_TAG, "Error register = 0x%04x", rslt.value);
        _diags.self_test_result = rslt.value;
        xSemaphoreGive(semaphore);
        return rslt.low == 0x01;
    }

    bool HardwareATAPIDevice::identify() {
        Responses::IdentifyPacket rslt = {};
        static_assert(sizeof(rslt) == 512);
        xSemaphoreTake(semaphore, portMAX_DELAY);

        ide->write(IDE::Register::Command, {{ .low = Command::IDENTIFY_PACKET_DEVICE, .high = 0xFF }});
        delayMicroseconds(1);
        const bool received = wait_for_data("IDENTIFY PACKET DEVICE") &&
                              ide->read_data(reinterpret_cast<uint8_t *>(&rslt), sizeof(rslt) / 2) &&
                              wait_sts_bit_clr({{.DRQ = true, .BSY = true}}, "IDENTIFY completion");
        const auto status = read_sts_regi();
        xSemaphoreGive(semaphore);

        if (!received || status.ERR || (rslt.general_config & 0xC000) != 0x8000) {
            ESP_LOGE(LOG_TAG, "IDENTIFY failed: status=0x%02x, word 0=0x%04x",
                     status.value, rslt.general_config);
            return false;
        }
        const auto * bytes = reinterpret_cast<const uint8_t *>(&rslt);
        if (bytes[510] == 0xA5) {
            uint8_t checksum = 0;
            for (size_t i = 0; i < sizeof(rslt); ++i) checksum += bytes[i];
            if (checksum != 0) {
                ESP_LOGE(LOG_TAG, "IDENTIFY checksum mismatch");
                return false;
            }
        }
        info.model = drive_string(rslt.model, sizeof(rslt.model), true);
        info.firmware = drive_string(rslt.firmware_rev, sizeof(rslt.firmware_rev), true);
        info.serial = drive_string(rslt.serial_no, sizeof(rslt.serial_no), true);

        packet_size = ((rslt.general_config & 0x1) != 0) ? 16 : 12;
        
        quirks = { 0 };


        static const std::vector<std::pair<std::string, Quirks>> quirks_db = {
            {"TEAC DV-W58G-A", Quirks {.must_use_softscan = true}},
            {"TEAC DV-W516GDM", Quirks {.must_use_softscan = true}},
            {"PLDS    DVD A  DH20A4P", Quirks {.must_use_softscan = true}},
            {"LITE-ON LTR-48246S", Quirks {.must_use_softscan = true}},
            {"JLMS XJ-HD166S", Quirks {.must_use_softscan = true}},
            {"Lite-On LTN486", Quirks {.must_use_softscan = true, .alternate_max_speed = 3000}},
            {"BENQ    DVD DD DW1620", Quirks { .busy_ass = true, .must_use_softscan = true, .alternate_max_speed = 3000 } /* TODO: still never inits unless a CD is in */ },
            {"CD-S520B", Quirks {.no_media_codes = true, .must_use_softscan = true}},

            {"TEAC CD-C68E", Quirks {.fucky_toc_reads = true}},
            {"NEC                 CD-ROM DRIVE:284", Quirks { .no_media_codes = true, .busy_ass = true }},
            {"NEC                 CD-ROM DRIVE:251", Quirks { .no_media_codes = true, .busy_ass = true, .must_use_softscan = true, .no_drq_in_toc = true }}, // NEC PC-CD400/4
        };

        for(auto const& id: quirks_db) {
            if(info.model.find(id.first, 0) != std::string::npos) {
                ESP_LOGW(LOG_TAG, "Drive is quirky!");
                quirks = id.second;
                break;
            }
        }

        ESP_LOGI(LOG_TAG, "Drive Model = '%s', SN = '%s', FW = '%s', packet size = %i", info.model.c_str(),  info.serial.c_str(), info.firmware.c_str(), packet_size);
        return true;
    }

    void HardwareATAPIDevice::log_inquiry() {
        Responses::Inquiry response = {};
        const Requests::Inquiry request = {
            .opcode = OperationCodes::INQUIRY,
            .allocation_length = sizeof(response),
        };

        xSemaphoreTake(semaphore, portMAX_DELAY);
        bool received = send_packet(&request, sizeof(request)) && wait_for_data("INQUIRY");
        if (received) {
            const uint8_t low = ide->read(IDE::Register::CylinderLow).low;
            const uint8_t high = ide->read(IDE::Register::CylinderHigh).low;
            const size_t bytes = (size_t(high) << 8) | low;
            if (bytes > 0 && bytes <= sizeof(response)) {
                received = ide->read_data(reinterpret_cast<uint8_t *>(&response), (bytes + 1) / 2) &&
                           wait_sts_bit_clr({{.DRQ = true, .BSY = true}}, "INQUIRY completion") &&
                           !read_sts_regi().ERR &&
                           bytes >= offsetof(Responses::Inquiry, revision);
            } else {
                ESP_LOGW(LOG_TAG, "Unexpected INQUIRY transfer length: %u", bytes);
                received = false;
            }
        }
        xSemaphoreGive(semaphore);
        if (!received) {
            ESP_LOGW(LOG_TAG, "Drive vendor unavailable (INQUIRY failed)");
            return;
        }

        const auto vendor = drive_string(response.vendor, sizeof(response.vendor));
        const auto product = drive_string(response.product, sizeof(response.product));
        ESP_LOGI(LOG_TAG, "Drive Vendor = '%s', Product = '%s'", vendor.c_str(), product.c_str());
    }

    void HardwareATAPIDevice::start(bool state) {
        const Requests::StartStopUnit req = {
            .opcode = OperationCodes::START_STOP_UNIT,
            .start = state,
            .load_eject = false
        };
        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&req, sizeof(req));
        xSemaphoreGive(semaphore);
    }

    void HardwareATAPIDevice::eject(bool open) {
        const Requests::StartStopUnit req = {
            .opcode = OperationCodes::START_STOP_UNIT,
            .start = !open,
            .load_eject = true
        };
        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&req, sizeof(req));
        xSemaphoreGive(semaphore);
    }

    void HardwareATAPIDevice::set_tray_locked(bool locked) {
        const Requests::PreventAllowMediaRemoval req = {
            .opcode = OperationCodes::PREVENT_ALLOW_MEDIA_REMOVAL,
            .prevent = locked,
            .persistent = false
        };
        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&req, sizeof(req));
        xSemaphoreGive(semaphore);
    }

    void HardwareATAPIDevice::load_unload(SlotNumber slot) {
        const Requests::LoadUnload lul = {
            .opcode = OperationCodes::LOAD_UNLOAD,
            .start = true,
            .load_unload = true,
            .slot = slot
        };

        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&lul, sizeof(lul), false);
        xSemaphoreGive(semaphore);
    }

    CapabilitiesMechStatusModePage HardwareATAPIDevice::mode_sense_capabilities() {
        const Requests::ModeSense msrq0 = {
            .opcode = OperationCodes::MODE_SENSE,
            .page = ModeSensePageCode::MSPC_CAPABILITIES_MECH_STS,
            .allocation_length = htobe16(0xFF)
        };
        CapabilitiesMechStatusModePage rslt;

        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&msrq0, sizeof(msrq0), true);
        Responses::ModeSense mph0;
        read_response(&mph0, sizeof(mph0), false);
        read_response(&rslt, sizeof(rslt), true);
        xSemaphoreGive(semaphore);
        
        // A value of zero indicates that the capability is NOT supported; a value of one indicates the capability IS supported.
        if(!rslt.audio_play) ESP_LOGW(LOG_TAG, "Audio play not supported!");
        if(!rslt.digital1 && !rslt.digital2) ESP_LOGW(LOG_TAG, "Digital port capability not present!");
        else if(!rslt.digital1 && rslt.digital2) ESP_LOGW(LOG_TAG, "Digital port [1] capability not present!");
        else if(rslt.digital1 && !rslt.digital2) ESP_LOGI(LOG_TAG, "Digital port [2] capability not present!");
        if(!rslt.cdda_cmds) ESP_LOGW(LOG_TAG, "CDDA command capability not present!");
        if(!rslt.pw_subcode_leadin) ESP_LOGI(LOG_TAG, "P-W subcode in lead-in capability not present, maybe CD TEXT won't work");
        if(!rslt.rw_subcode_supp) ESP_LOGI(LOG_TAG, "R-W subcode capability not present, maybe CD TEXT won't work");
        if(!rslt.rw_deint_corr) ESP_LOGI(LOG_TAG, "R-W deinterleave/correction capability not present, maybe CD TEXT won't work");

        return rslt;
    }

    void HardwareATAPIDevice::set_speed() {
        // Some drives are too noisy to let them run at full force
        const Requests::SetCDSpeed req = {
            .opcode = OperationCodes::SET_CD_SPEED,
            .read_speed = htobe16((quirks.alternate_max_speed == 0) ? 600 : quirks.alternate_max_speed),
            .write_speed = 0xFFFF
        };
        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&req, sizeof(req), true);
        xSemaphoreGive(semaphore);
    }

    const std::vector<uint8_t> HardwareATAPIDevice::read_cd_text_toc_locked() {
        // just for fun let's try this
        const Requests::ReadTOC req = {
            .opcode = OperationCodes::READ_TOC_PMA_ATIP,
            .msf = true,
            .format = TocFormat::TOC_FMT_CD_TEXT,
            .allocation_length = htobe16(0x7FFF) // oughtta be enough!
        };

        send_packet(&req, sizeof(req), true);
        delay(50); // some drives e.g. CD68E seem to be kinda slow on the response, producing invalid output
        wait_not_busy("CDTX");

        StatusRegister sr = read_sts_regi();
        if(sr.ERR) {
            ESP_LOGW(LOG_TAG, "Probably drive can't parse CD TEXT");
        }

        // This is not part of ATAPI spec, so hiding it here instead of the appropriate header
        struct ATAPI_PKT {
            uint16_t size;
            uint16_t reserved;
        } head;
        read_response(&head, sizeof(head), false);

        std::vector<uint8_t> rslt = {};

        if(head.reserved != 0) {
            ESP_LOGW(LOG_TAG, "CD text: expected bytes [2] and [3] to be 0 but they were 0x%04x, probably the device can't read CD Text, bailing out!", head.reserved);
            read_response(nullptr, 0, true);
        }
        else {
            head.size = be16toh(head.size);
            std::vector<uint8_t> buffer(head.size);
            read_response(&buffer[0], head.size, true);
            rslt = buffer;
            // printf("CDTXT: ");
            // for(size_t i=0;i<head.size; i++) {
            //     printf("%02x ", buffer[i]);
            // }
            // printf("\n");
        }

        return rslt;
    }

    const DiscTOC HardwareATAPIDevice::read_toc() {
        set_speed();

        const Requests::ReadTOC req = {
            .opcode = OperationCodes::READ_TOC_PMA_ATIP,
            .msf = true,
            .format = TocFormat::TOC_FMT_TOC,
            .allocation_length = 0xFFFF // oughtta be enough!
        };

        Responses::ReadTOCResponseHeader res_hdr;
        bool success = false;
        int attempts = quirks.fucky_toc_reads ? 20 : 2;
        std::vector<uint8_t> toc_subchannel = {};
        std::vector<DiscTrack> tracks = {};
        MSF leadOut = { .M = 0, .S = 0, .F = 0 };
        Responses::NormalTOCEntry entry;

        tracks = {};

        xSemaphoreTake(semaphore, portMAX_DELAY);

        do {
            tracks.clear();
            send_packet(&req, sizeof(req), true);
            delay(quirks.fucky_toc_reads ? 1000 : 50); // some drives e.g. CD68E seem to be kinda slow on the response, producing invalid output
            wait_not_busy("TOC");

            read_response(&res_hdr, sizeof(res_hdr), false);
            res_hdr.data_length = be16toh(res_hdr.data_length);
            ESP_LOGI(LOG_TAG, "Reported TOC size = %i (from track %i to %i)", res_hdr.data_length, res_hdr.first_track_no, res_hdr.last_track_no);

            if(res_hdr.data_length == (res_hdr.last_track_no - (res_hdr.first_track_no - 1) + 1) * sizeof(entry) + 2 && res_hdr.last_track_no >= res_hdr.first_track_no) {
                ESP_LOGI(LOG_TAG, "TOC reading attempts = %i", attempts);
                int last_trkno = res_hdr.first_track_no - 1;
                for(int i = res_hdr.first_track_no; i <= res_hdr.last_track_no + 1; i++) {
                    if(!quirks.no_drq_in_toc) wait_drq();
                    read_response(&entry, sizeof(entry), false);
                    ESP_LOGI(LOG_TAG, " + track %i: %02im%02is%02if, preemph=%i, prot=%i, data=%i, quadro=%i", entry.track_no, entry.address.M, entry.address.S, entry.address.F, entry.pre_emphasis, entry.copy_protected, entry.data_track, entry.quadrophonic);

                    if((entry.track_no != TRK_NUM_LEAD_OUT && entry.track_no != last_trkno + 1) || (last_trkno == TRK_NUM_LEAD_OUT)) {
                        ESP_LOGE(LOG_TAG, "  ^--- this makes no sense! retry.");
                        success = false;
                        break;
                    }

                    if(entry.track_no == TRK_NUM_LEAD_OUT) {
                        leadOut = entry.address;
                        success = true;
                    } else {
                        tracks.push_back( DiscTrack {
                            .number = entry.track_no,
                            .position = entry.address,
                            .is_data = entry.data_track,
                            .preemphasis = entry.pre_emphasis
                        } );
                    }

                    last_trkno = entry.track_no;
                }
                // flush anything if any
                read_response(nullptr, 0, true);
            } else {
                ESP_LOGE(LOG_TAG, "  ^--- this makes no sense! retry.");
            }
        } while(!success && (attempts--) > 0);
        
        if(!success) tracks.clear();
        else toc_subchannel = read_cd_text_toc_locked();

        xSemaphoreGive(semaphore);
        return DiscTOC {
            .leadOut = leadOut,
            .tracks = tracks,
            .toc_subchannel = toc_subchannel
        };
    }

    MediaTypeCode HardwareATAPIDevice::check_media() {
        const Requests::ModeSense req = {
            .opcode = OperationCodes::MODE_SENSE,
            .page = ModeSensePageCode::MSPC_ERROR_RECOVERY,
            .allocation_length = 0xFFFF
        };
        Responses::ModeSense res;

        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&req, sizeof(req), true);
        read_response(&res, sizeof(res), true);
        xSemaphoreGive(semaphore);

        ESP_LOGV(LOG_TAG, "Mode sense: media type 0x%02x", res.media_type);
        return res.media_type;
    }

    bool HardwareATAPIDevice::send_packet(const void * buf, size_t bufLen, bool pad) {
        if (!write_packet(buf, bufLen, pad)) return false;
        if (quirks.busy_ass) {
            delay(7);
        }
        return wait_not_busy("PACKET completion");
    }

    bool HardwareATAPIDevice::write_packet(const void * buf, size_t bufLen, bool pad) {
        delayMicroseconds(100); // either just the C68E is too old and slow to keep up with the ESP even over 100kHz i2c... or my pcb layout bites me in the ass again!!
        // for our use case those occasional delays don't have too much effect but this will limit potential growth for data disc reading if any!


        // Need to set nIEN before sending the PACKET command
        ide->write(IDE::Register::DeviceControl, {{ .low = (DeviceControlRegister {{ .nIEN = true }}).value, .high = 0xFF }});
        delayMicroseconds(100);
        ide->write(IDE::Register::Command, {{ .low = Command::WRITE_PACKET, .high = 0xFF }});
        delayMicroseconds(1);
        if (!wait_for_data("PACKET request")) return false;

        const uint8_t* data = (const uint8_t*) buf;
        int i = 0;
        for(i = 0; i < bufLen; i+=2) {
            delayMicroseconds(10);
            const uint8_t high = i + 1 < bufLen ? data[i + 1] : 0;
            ide->write(IDE::Register::Data, {{ .low = data[i], .high = high }});
        }

        if(pad) {
            for(; i < packet_size; i+=2) {
                delayMicroseconds(10);
                ide->write(IDE::Register::Data, { 0x00, 0x00 });
            }
        }

        delayMicroseconds(1);
        return true;
    }

    bool HardwareATAPIDevice::read_response(void * outBuf, size_t bufLen, bool flush) {
        if((bufLen == 0 || outBuf == nullptr) && !flush) {
            ESP_LOGE(LOG_TAG, "No buffer provided for response!");
            return false;
        }

        data16 val;
        StatusRegister sts;
        uint8_t * buf = (uint8_t*)outBuf;
        bool rslt = false;

        if(bufLen > 0 && buf != nullptr) {
            int i = 0;
            do {
                val = ide->read(IDE::Register::Data);
                buf[i++] = val.low;
                if (i < bufLen) buf[i++] = val.high;
                delayMicroseconds(10);

                sts = read_sts_regi();
                delayMicroseconds(10);

                if(quirks.busy_ass) {
                    delayMicroseconds(33);
                }
            } while(bufLen > i && !sts.BSY && (sts.DRQ || quirks.no_drq_in_toc));

            if(bufLen > i) {
                ESP_LOGV(LOG_TAG, "Data underrun when reading response: wanted %i bytes, DRQ clear after %i bytes", bufLen, i + 1);
            }
            else if(sts.DRQ && !flush) {
                ESP_LOGV(LOG_TAG, "Buffer overrun when reading response: wanted %i bytes, but DRQ still set", bufLen);
            }
            else {
                rslt = true;
            }
        }

        if(flush) {
            size_t flushed = 0;
            sts = read_sts_regi();
            // The task file caps each packet data phase at 512 bytes.
            // Allow multiple phases to drain while bounding a stuck DRQ.
            while(sts.DRQ && !sts.BSY && flushed < 65536) {
                ide->read(IDE::Register::Data);
                flushed += 2;

                sts = read_sts_regi();
            }
            if(flushed > 0) ESP_LOGD(LOG_TAG, "Flushed %i extra bytes", flushed);
            if (sts.DRQ && !sts.BSY) {
                ESP_LOGE(LOG_TAG, "DRQ stayed set while draining ATAPI response");
                return false;
            }
            if (bufLen == 0) rslt = !sts.BSY;
        }

        return rslt;
    }

    HardwareATAPIDevice::StatusRegister HardwareATAPIDevice::read_sts_regi() {
        StatusRegister val;
        val.value = ide->read(IDE::Register::Status).low;
        return val;
    }

    void HardwareATAPIDevice::wait_sts_bit_set(StatusRegister bits) {
        TickType_t start_wait = xTaskGetTickCount();
        ESP_LOGD(LOG_TAG, "Wait for bit set 0x%02x", bits.value);

        do {
            if(xTaskGetTickCount() - start_wait >= pdMS_TO_TICKS(10000)) {
                ESP_LOGW(LOG_TAG, "Still waiting for bit set 0x%02x", bits.value);
                start_wait = xTaskGetTickCount();
            }
        } while ((read_sts_regi().value & bits.value) == 0);
    }

    bool HardwareATAPIDevice::wait_sts_bit_clr(StatusRegister bits, const char * tag) {
        const TickType_t start_wait = xTaskGetTickCount();
        StatusRegister status;
        do {
            status = read_sts_regi();
            if ((status.value & bits.value) == 0) return true;
            delay(10);
        } while (xTaskGetTickCount() - start_wait < pdMS_TO_TICKS(30000));
        ESP_LOGE(LOG_TAG, "%s timed out, status=0x%02x", tag, status.value);
        return false;
    }

    bool HardwareATAPIDevice::wait_not_busy(const char * tag) {
        return wait_sts_bit_clr({{.BSY = true}}, tag);
    }

    bool HardwareATAPIDevice::wait_for_data(const char * tag) {
        const TickType_t start_wait = xTaskGetTickCount();
        StatusRegister status;
        do {
            status = read_sts_regi();
            if (!status.BSY) {
                if (status.ERR || status.DF) break;
                if (status.DRQ) return true;
            }
            delay(1);
        } while (xTaskGetTickCount() - start_wait < pdMS_TO_TICKS(5000));
        ESP_LOGE(LOG_TAG, "%s did not reach DRQ, status=0x%02x", tag, status.value);
        return false;
    }
    void HardwareATAPIDevice::wait_drq() { wait_sts_bit_set({{.DRQ = true}}); }

    void HardwareATAPIDevice::wait_ready() {
        ESP_LOGI(LOG_TAG, "Waiting for drive to become ready...");
        xSemaphoreTake(semaphore, portMAX_DELAY);
        wait_sts_bit_set({{.DRDY = true}}); 

        const Requests::RequestSense req = {
            .opcode = OperationCodes::REQUEST_SENSE,
            .allocation_length = 0xFF
        };
        Responses::RequestSense res = { 0 };
        TickType_t start_wait = xTaskGetTickCount();
        do {
            if(xTaskGetTickCount() - start_wait >= pdMS_TO_TICKS(10000)) {
                ESP_LOGW(LOG_TAG, "Device still not ready!");
                start_wait = xTaskGetTickCount();
            }
            send_packet(&req, sizeof(req), true);
            read_response(&res, sizeof(res), true);
        } while(res.sense_key == RequestSenseKey::SENSE_NOT_READY /*&& res.additional_sense_code == RequestSenseAsc::ASC_DEVICE_NOT_READY*/);
        ESP_LOGI(LOG_TAG, "Request sense ready with SK=0x%02x, ASC=0x%02x", res.sense_key, res.additional_sense_code);
        xSemaphoreGive(semaphore);

        // const uint8_t req2 = OperationCodes::TEST_UNIT_READY;
        // send_packet(&req2, sizeof(req2), true);
        // StatusRegister sr = read_sts_regi();
        // while(sr.ERR || sr.BSY) {
        //     if(xTaskGetTickCount() - start_wait >= pdMS_TO_TICKS(10000)) {
        //         ESP_LOGW(LOG_TAG, "Waiting for TEST_UNIT_READY to stop raising ERR and/or BSY");
        //         start_wait = xTaskGetTickCount();
        //     }
        //     send_packet(&req2, sizeof(req2), true);
        //     sr = read_sts_regi();
        // }

        query_state();
        while(mech_sts.is_in_motion) {
            if(xTaskGetTickCount() - start_wait >= pdMS_TO_TICKS(10000)) {
                ESP_LOGW(LOG_TAG, "Waiting for changer!");
                start_wait = xTaskGetTickCount();
            }
            query_state();
        }

        ESP_LOGI(LOG_TAG, "The device appears to be ready");
    }
}
