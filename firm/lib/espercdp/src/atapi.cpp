#include <esper-cdp/atapi.h>
#include <esper-cdp/atapi-protocol.h>
#include <esper-core/lock.h>
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

    HardwareATAPIDevice::HardwareATAPIDevice(Platform::IDEBus * bus, IDE::DeviceNumber pos):
        ide(bus), bus_position(pos),
        Device()
    {
        // compile time sanity checks
        const StatusRegister tmp = StatusRegister { .value = (1 << 7) };
        assert(tmp.BSY);

        semaphore = ide->transaction_mutex();

        quirks = {0};
    }

    void HardwareATAPIDevice::select_bus_position() {
        ide->active_device = bus_position;
    }

    void HardwareATAPIDevice::reset() {
        initialize();
    }

    bool HardwareATAPIDevice::probe() {
        {
            Lock lock(semaphore);
            select_bus_position();
            if (!wait_not_busy("probe", 5000)) {
                signature_verified = false;
                return false;
            }
        }
        signature_verified = check_atapi_compatible();
        return signature_verified;
    }

    bool HardwareATAPIDevice::initialize() {
        bool reset_complete;
        {
            Lock lock(semaphore);
            select_bus_position();
            ESP_LOGI(LOG_TAG, "Initialize device %d", bus_position);
            info = {};
            _diags = {};
            quirks = {};
            packet_size = 12;
            reset_complete = wait_not_busy("reset", 2000);
        }

        if (!reset_complete) return false;
        _diags.is_atapi = signature_verified || check_atapi_compatible();
        _diags.is_autonomous = false;
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
        ESP_LOGI(LOG_TAG, "Device %d initialized", bus_position);
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

        Lock lock(semaphore);
        select_bus_position();

        if (!send_packet(&req, sizeof(req)) || !read_response(&hdr, sizeof(hdr), false)) {
            read_response(nullptr, 0, true);
            return &mech_sts;
        }

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
                if (!read_response(&slot_info, sizeof(slot_info), false)) break;

                ESP_LOGD(LOG_TAG, " - SLOT#%i: Present = %i, Changed = %i", i, slot_info.disc_present, slot_info.disc_changed);
                mech_sts.changer_slots[i].disc_changed = slot_info.disc_changed;
                mech_sts.changer_slots[i].disc_in = slot_info.disc_present;
            }
        }

        read_response(nullptr, 0, true);

        return &mech_sts;
    }

    bool HardwareATAPIDevice::check_atapi_compatible() {
        uint8_t low;
        uint8_t high;
        {
            Lock lock(semaphore);
            select_bus_position();
            low = ide->read(IDE::Register::CylinderLow).low;
            high = ide->read(IDE::Register::CylinderHigh).low;
        }

        ESP_LOGI(LOG_TAG, "Drive signature = 0x%02x%02x", high, low);
        return low == 0x14 && high == 0xEB;
    }

    bool HardwareATAPIDevice::init_task_file() {
        Lock lock(semaphore);
        select_bus_position();
        ESP_LOGI(LOG_TAG, "init task file");
        ide->write(IDE::Register::Feature, {{ .low = (FeatureRegister {{ .DMA = false, .overlap = false }}).value, .high = 0xFF }});

        // PIO buffer = 0x200
        ide->write(IDE::Register::CylinderHigh, {{ .low = 0x02, .high = 0xFF }});
        ide->write(IDE::Register::CylinderLow, {{ .low = 0x00, .high = 0xFF }});

        // Disable interrupts
        ide->write(IDE::Register::DeviceControl, {{ .low = (DeviceControlRegister {{ .nIEN = true }}).value, .high = 0xFF }});

        const bool idle = wait_sts_bit_clr({{.DRQ = true, .BSY = true}}, "task file");
        return idle;
    }

    bool HardwareATAPIDevice::self_test() {
        Lock lock(semaphore);
        select_bus_position();
        // EXECUTE DEVICE DIAGNOSTIC is a bus-wide command and can leave the
        // other unit unable to be selected. The hardware reset performed
        // before probing already supplies a diagnostic result in Error.
        ESP_LOGI(LOG_TAG, "Reset diagnostic result");
        if (!wait_not_busy("reset diagnostic")) {
            return false;
        }
        data16 rslt = ide->read(IDE::Register::Error);
        ESP_LOGI(LOG_TAG, "Error register = 0x%04x", rslt.value);
        _diags.self_test_result = rslt.value;
        return rslt.low == 0x01;
    }

    bool HardwareATAPIDevice::identify() {
        Responses::IdentifyPacket rslt = {};
        static_assert(sizeof(rslt) == 512);
        bool received;
        StatusRegister status;
        {
            Lock lock(semaphore);
            select_bus_position();
            ide->write(IDE::Register::Command, {{ .low = Command::IDENTIFY_PACKET_DEVICE, .high = 0xFF }});
            delayMicroseconds(1);
            received = wait_for_data("IDENTIFY PACKET DEVICE") &&
                       ide->read_data(reinterpret_cast<uint8_t *>(&rslt), sizeof(rslt) / 2) &&
                       wait_sts_bit_clr({{.DRQ = true, .BSY = true}}, "IDENTIFY completion");
            status = read_sts_regi();
        }

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

        bool received;
        {
            Lock lock(semaphore);
            select_bus_position();
            received = send_packet(&request, sizeof(request)) && wait_for_data("INQUIRY");
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
        }
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
        Lock lock(semaphore);
        select_bus_position();
        send_packet(&req, sizeof(req));
    }

    void HardwareATAPIDevice::eject(bool open) {
        const Requests::StartStopUnit req = {
            .opcode = OperationCodes::START_STOP_UNIT,
            .start = !open,
            .load_eject = true
        };
        Lock lock(semaphore);
        select_bus_position();
        send_packet(&req, sizeof(req));
    }

    void HardwareATAPIDevice::set_tray_locked(bool locked) {
        const Requests::PreventAllowMediaRemoval req = {
            .opcode = OperationCodes::PREVENT_ALLOW_MEDIA_REMOVAL,
            .prevent = locked,
            .persistent = false
        };
        Lock lock(semaphore);
        select_bus_position();
        send_packet(&req, sizeof(req));
    }

    void HardwareATAPIDevice::load_unload(SlotNumber slot) {
        const Requests::LoadUnload lul = {
            .opcode = OperationCodes::LOAD_UNLOAD,
            .start = true,
            .load_unload = true,
            .slot = (uint8_t) (slot |  (quirks.changer_slot_bit6 ? 0x40 : 0x00))
        };

        Lock lock(semaphore);
        select_bus_position();
        send_packet(&lul, sizeof(lul), false);
    }

    CapabilitiesMechStatusModePage HardwareATAPIDevice::mode_sense_capabilities() {
        const Requests::ModeSense msrq0 = {
            .opcode = OperationCodes::MODE_SENSE,
            .page = ModeSensePageCode::MSPC_CAPABILITIES_MECH_STS,
            .allocation_length = htobe16(0xFF)
        };
        CapabilitiesMechStatusModePage rslt;

        {
            Lock lock(semaphore);
            select_bus_position();
            send_packet(&msrq0, sizeof(msrq0), true);
            Responses::ModeSense mph0;
            read_response(&mph0, sizeof(mph0), false);
            read_response(&rslt, sizeof(rslt), true);
        }
        
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
        uint16_t speed = (quirks.alternate_max_speed == 0) ? 600 : quirks.alternate_max_speed;
        ESP_LOGI(LOG_TAG, "Set CD Speed: %i", speed);
        const Requests::SetCDSpeed req = {
            .opcode = OperationCodes::SET_CD_SPEED,
            .read_speed = htobe16(speed),
            .write_speed = 0xFFFF
        };
        Lock lock(semaphore);
        select_bus_position();
        send_packet(&req, sizeof(req), true);
    }

    const std::vector<uint8_t> HardwareATAPIDevice::read_cd_text_toc_locked() {
        // just for fun let's try this
        const Requests::ReadTOC req = {
            .opcode = OperationCodes::READ_TOC_PMA_ATIP,
            .msf = true,
            .format = TocFormat::TOC_FMT_CD_TEXT,
            .allocation_length = htobe16(0x7FFF) // oughtta be enough!
        };

        if (!send_packet(&req, sizeof(req), true)) return {};
        delay(50); // some drives e.g. CD68E seem to be kinda slow on the response, producing invalid output

        struct ATAPI_PKT {
            uint16_t size;
            uint16_t reserved;
        } head{};
        if (!read_response(&head, sizeof(head), false)) {
            read_response(nullptr, 0, true);
            return {};
        }

        const size_t length = be16toh(head.size);
        if (head.reserved != 0 || length < 2 || length > 0x7FFF - 2) {
            read_response(nullptr, 0, true);
            return {};
        }
        std::vector<uint8_t> buffer(length - 2);
        if (!read_response(buffer.data(), buffer.size(), true)) return {};
        return buffer;
    }

    const DiscTOC HardwareATAPIDevice::read_toc() {
        set_speed();

        const Requests::ReadTOC req = {
            .opcode = OperationCodes::READ_TOC_PMA_ATIP,
            .msf = true,
            .format = TocFormat::TOC_FMT_TOC,
            .allocation_length = 0xFFFF // oughtta be enough!
        };

        bool success = false;
        int attempts = quirks.fucky_toc_reads ? 20 : 2;
        std::vector<uint8_t> toc_subchannel = {};
        std::vector<DiscTrack> tracks = {};
        MSF leadOut = { .M = 0, .S = 0, .F = 0 };

        {
            Lock lock(semaphore);
            select_bus_position();

            do {
                tracks.clear();
                if (!send_packet(&req, sizeof(req), true)) continue;
                delay(quirks.fucky_toc_reads ? 1000 : 50); // some drives e.g. CD68E seem to be kinda slow on the response, producing invalid output

                Responses::ReadTOCResponseHeader header{};
                if (!read_response(&header, sizeof(header), false)) {
                    ESP_LOGW(LOG_TAG, "Incomplete TOC header");
                    read_response(nullptr, 0, true);
                    continue;
                }
                const int first = header.first_track_no;
                const int last = header.last_track_no;
                const uint16_t length = be16toh(header.data_length);
                ESP_LOGI(LOG_TAG, "Reported TOC size = %u (from track %d to %d)", length, first, last);
                if (first < 1 || last < first || last > 99 ||
                    length != 2 + (last - first + 2) * sizeof(Responses::NormalTOCEntry)) {
                    ESP_LOGW(LOG_TAG, "Invalid TOC header");
                    read_response(nullptr, 0, true);
                    continue;
                }

                for (int i = first; i <= last + 1; ++i) {
                    Responses::NormalTOCEntry entry{};
                    const int expected = i <= last ? i : TRK_NUM_LEAD_OUT;
                    if (!read_response(&entry, sizeof(entry), false)) {
                        ESP_LOGW(LOG_TAG, "Incomplete TOC entry for track %d", expected);
                        break;
                    }
                    if (entry.track_no != expected || entry.address.M > 99 ||
                        entry.address.S >= 60 || entry.address.F >= MSF::FRAMES_IN_SECOND) {
                        ESP_LOGW(LOG_TAG, "Invalid TOC entry for track %d: track=%u, MSF=%u:%u:%u", expected,
                                 entry.track_no, entry.address.M, entry.address.S, entry.address.F);
                        break;
                    }
                    ESP_LOGI(LOG_TAG, " + track %u: %02um%02us%02uf, preemph=%i, prot=%i, data=%i, quadro=%i",
                             entry.track_no, entry.address.M, entry.address.S, entry.address.F,
                             entry.pre_emphasis, entry.copy_protected, entry.data_track, entry.quadrophonic);
                    if (expected == TRK_NUM_LEAD_OUT) {
                        leadOut = entry.address;
                        success = true;
                    } else {
                        tracks.push_back(DiscTrack {
                            .number = entry.track_no,
                            .position = entry.address,
                            .is_data = entry.data_track,
                            .preemphasis = entry.pre_emphasis
                        });
                    }
                }
                const bool complete = read_response(nullptr, 0, true);
                success = success && complete;
            } while(!success && (attempts--) > 0);

            if (success) {
                toc_subchannel = read_cd_text_toc_locked();
            } else {
                tracks.clear();
                leadOut = {};
            }
        }
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
        Responses::ModeSense res{};

        {
            Lock lock(semaphore);
            select_bus_position();
            if (!send_packet(&req, sizeof(req), true) || !read_response(&res, sizeof(res), true)) {
                return MediaTypeCode::MTC_DOOR_CLOSED_UNKNOWN;
            }
        }

        ESP_LOGV(LOG_TAG, "Mode sense: media type 0x%02x", res.media_type);
        return res.media_type;
    }

    bool HardwareATAPIDevice::send_packet(const void * buf, size_t bufLen, bool pad) {
        if (!wait_sts_bit_clr({{.DRQ = true, .BSY = true}}, "before PACKET", 5000)) return false;
        // A data phase replaces Cylinder Low/High with its actual byte count.
        // Program the 512-byte limit again for each ordinary packet.
        ide->write(IDE::Register::CylinderLow, {{0x00, 0xFF}});
        ide->write(IDE::Register::CylinderHigh, {{0x02, 0xFF}});
        if (!write_packet(buf, bufLen, pad)) return false;
        before_packet_completion_poll();
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
        if ((bufLen == 0 && !flush) || (bufLen != 0 && outBuf == nullptr)) {
            ESP_LOGE(LOG_TAG, "No buffer provided for response!");
            return false;
        }

        auto * buffer = static_cast<uint8_t *>(outBuf);
        size_t received = 0;
        while (received < bufLen) {
            const bool data_ready = quirks.no_drq_in_toc
                ? wait_not_busy("PACKET response", 5000)
                : wait_for_data("PACKET response", true);
            if (!data_ready) break;

            const auto word = ide->read(IDE::Register::Data);
            buffer[received++] = word.low;
            if (received < bufLen) buffer[received++] = word.high;
            delayMicroseconds(quirks.busy_ass ? 53 : 20);
        }

        const bool complete = received == bufLen;
        if (!complete) ESP_LOGW(LOG_TAG, "Short PACKET response: %zu/%zu bytes", received, bufLen);
        // A caller can read a header and then read the rest of the same phase.
        if (!flush) return complete;

        const TickType_t start = xTaskGetTickCount();
        size_t drained = 0;
        do {
            const auto status = read_sts_regi();
            if (status.BSY) {
                delay(1);
                continue;
            }
            if (status.ERR || status.DF) return false;
            if (!status.DRQ) return complete;
            if (drained == 65536) break;
            ide->read(IDE::Register::Data);
            drained += 2;
            delayMicroseconds(quirks.busy_ass ? 53 : 20);
        } while (xTaskGetTickCount() - start < pdMS_TO_TICKS(5000));

        ESP_LOGE(LOG_TAG, "PACKET response did not complete after draining %zu bytes", drained);
        return false;
    }

    HardwareATAPIDevice::StatusRegister HardwareATAPIDevice::read_sts_regi() {
        StatusRegister val;
        val.value = ide->read(IDE::Register::Status).low;
        return val;
    }

    bool HardwareATAPIDevice::wait_sts_bit_clr(StatusRegister bits, const char * tag, int maxTimeMs) {
        const TickType_t start_wait = xTaskGetTickCount();
        StatusRegister status;
        do {
            status = read_sts_regi();
            if ((status.value & bits.value) == 0) return true;
            delay(10);
        } while (xTaskGetTickCount() - start_wait < pdMS_TO_TICKS(maxTimeMs));
        ESP_LOGE(LOG_TAG, "%s timed out, status=0x%02x", tag, status.value);
        return false;
    }

    bool HardwareATAPIDevice::wait_not_busy(const char * tag,  int maxTimeMs) {
        return wait_sts_bit_clr({{.BSY = true}}, tag, maxTimeMs);
    }

    bool HardwareATAPIDevice::wait_for_data(const char * tag, bool packet_response) {
        const TickType_t start_wait = xTaskGetTickCount();
        StatusRegister status;
        do {
            status = read_sts_regi();
            if (!status.BSY) {
                if (status.ERR || status.DF) break;
                if (!packet_response && status.DRQ) return true;
                if (packet_response) {
                    const uint8_t reason = ide->read(IDE::Register::SectorCount).low & 0x03;
                    if (status.DRQ) {
                        if (reason == 0x02) return true;
                        ESP_LOGE(LOG_TAG, "%s reached unexpected PACKET phase 0x%02x", tag, reason);
                        return false;
                    }
                    if (reason == 0x03) break;
                }
            }
            delay(1);
        } while (xTaskGetTickCount() - start_wait < pdMS_TO_TICKS(5000));
        if (!status.BSY && status.ERR) {
            ESP_LOGW(LOG_TAG, "%s failed, status=0x%02x, error=0x%02x", tag, status.value,
                     ide->read(IDE::Register::Error).low);
        } else {
            ESP_LOGW(LOG_TAG, "%s did not reach DRQ, status=0x%02x", tag, status.value);
        }
        return false;
    }
    void HardwareATAPIDevice::wait_ready() {
        ESP_LOGI(LOG_TAG, "Waiting for drive to become ready...");
        const uint8_t test_ready = OperationCodes::TEST_UNIT_READY;
        const Requests::RequestSense request_sense = {
            .opcode = OperationCodes::REQUEST_SENSE,
            .allocation_length = 18,
        };
        static_assert(offsetof(Responses::RequestSense, additional_sense_code) == 12);
        const TickType_t start = xTaskGetTickCount();
        Responses::RequestSense sense{};
        bool ready = false;
        bool have_sense = false;
        do {
            {
                Lock lock(semaphore);
                select_bus_position();
                have_sense = false;
                if (!send_packet(&test_ready, sizeof(test_ready))) break;
                const auto status = read_sts_regi();
                if (!status.BSY && !status.DRQ && !status.ERR && !status.DF &&
                    (ide->read(IDE::Register::SectorCount).low & 0x03) == 0x03) {
                    ready = true;
                    break;
                }

                sense = {};
                have_sense = status.ERR && !status.DRQ &&
                    send_packet(&request_sense, sizeof(request_sense)) && read_response(&sense, sizeof(sense), true) &&
                    (sense.error_code == 0x70 || sense.error_code == 0x71);
                if (have_sense && sense.sense_key == SENSE_NOT_READY && sense.additional_sense_length >= 6 &&
                    sense.additional_sense_code == ASC_MEDIUM_NOT_PRESENT) {
                    ESP_LOGI(LOG_TAG, "No medium present");
                    return;
                }
                if (!have_sense || (sense.sense_key != SENSE_NOT_READY &&
                    sense.sense_key != SENSE_UNIT_ATTENTION && sense.sense_key != SENSE_ABORTED_COMMAND)) break;
            }
            delay(100);
        } while (xTaskGetTickCount() - start < pdMS_TO_TICKS(5000));

        if (!ready) {
            if (have_sense) {
                ESP_LOGW(LOG_TAG, "TEST UNIT READY failed: SK=%02x ASC=%02x ASCQ=%02x",
                         sense.sense_key, sense.additional_sense_code, sense.additional_sense_code_qualifier);
            } else {
                ESP_LOGW(LOG_TAG, "TEST UNIT READY failed without valid sense data");
            }
            return;
        }

        while (query_state()->is_in_motion) {
            if (xTaskGetTickCount() - start >= pdMS_TO_TICKS(10000)) {
                ESP_LOGW(LOG_TAG, "Changer still in motion");
                return;
            }
            delay(50);
        }

        ESP_LOGI(LOG_TAG, "Drive is ready");
    }
}
