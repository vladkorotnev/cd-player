#include <esper-cdp/atapi.h>
#include <esper-cdp/atapi-protocol.h>
#include <cassert>
#include <algorithm>
#include <endian.h>

static const char LOG_TAG[] = "ATAPI";

namespace ATAPI {
    static void ata_str_to_human(char * atastr, size_t len) {
        for(int i = 0; i < len; i += 2) {
            char tmp = atastr[i];
            atastr[i] = atastr[i + 1];
            atastr[i + 1] = tmp;
        }
        for(int i = len; i >= 0; i--) { 
            if(isspace(atastr[i]) || !isprint(atastr[i])) atastr[i] = 0;
            else break;
        }
    }

    Device::Device(Platform::IDEBus * bus): 
        ide(bus) 
    {
        // compile time sanity checks
        const StatusRegister tmp = StatusRegister { .value = (1 << 7) };
        assert(tmp.BSY);

        semaphore = xSemaphoreCreateBinary();
        xSemaphoreGive(semaphore);

        quirks = {0};
    }

    void Device::reset() {
        xSemaphoreTake(semaphore, portMAX_DELAY);
        ESP_LOGI(LOG_TAG, "Reset");
        ide->reset();
        delay(3000);
        wait_not_busy();
        ide->write(IDE::Register::Command, {{.low = 0x91, .high = 0xFF}});
        xSemaphoreGive(semaphore);

        check_atapi_compatible();
        self_test();
        init_task_file();
        identify();
        query_state();
        ESP_LOGI(LOG_TAG, "end of Reset");
    }

    const DriveInfo * Device::get_info() { return &info; }

    const MechInfo * Device::query_state() {
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

    const AudioStatus * Device::query_position() {
        Responses::ReadSubchannel res = { 0 };

        const Requests::ReadSubchannel req = {
            .opcode = OperationCodes::READ_SUBCHANNEL,
            .msf = true,
            .subQ = true,
            .data_format = SubchannelFormat::SUBCH_FMT_CD_POS,
            .allocation_length = sizeof(res)
        };

        xSemaphoreTake(semaphore, portMAX_DELAY);

        send_packet(&req, sizeof(req), true);
        read_response(&res, sizeof(res), true);

        xSemaphoreGive(semaphore);

        switch(res.audio_status) {
            case Responses::ReadSubchannel::SubchannelAudioStatus::AUDIOSTS_PLAYING:
                audio_sts.state = AudioStatus::PlayState::Playing;
                break;

            case Responses::ReadSubchannel::SubchannelAudioStatus::AUDIOSTS_PAUSED:
                audio_sts.state = AudioStatus::PlayState::Paused;
                break;

            case Responses::ReadSubchannel::SubchannelAudioStatus::AUDIOSTS_UNSUPPORTED_INVALID:
            case Responses::ReadSubchannel::SubchannelAudioStatus::AUDIOSTS_NONE:
            case Responses::ReadSubchannel::SubchannelAudioStatus::AUDIOSTS_STOPPED_ERROR:
            case Responses::ReadSubchannel::SubchannelAudioStatus::AUDIOSTS_COMPLETED:
                audio_sts.state = AudioStatus::PlayState::Stopped;
                break;
        }

        if(res.data_format != SubchannelFormat::SUBCH_FMT_CD_POS) {
            ESP_LOGV(LOG_TAG, "Read Subchannel response not of expected type (got 0x%x)", res.data_format);
        } else {
            audio_sts.track = res.current_position_data.track_no;
            audio_sts.index = res.current_position_data.index_no;
            audio_sts.position_in_disc = res.current_position_data.absolute_address;
            audio_sts.position_in_track = res.current_position_data.relative_address;

            ESP_LOGV(LOG_TAG, "Audio Sts = %i, track = %i.%i, total time = %02im%02is%02if, track time = %02im%02is%02if", res.audio_status, audio_sts.track, audio_sts.index, audio_sts.position_in_disc.M, audio_sts.position_in_disc.S, audio_sts.position_in_disc.F, audio_sts.position_in_track.M, audio_sts.position_in_track.S, audio_sts.position_in_track.F);
        }

        return &audio_sts;
    }

    bool Device::check_atapi_compatible() {
        data16 tmp;
        bool rslt;

        xSemaphoreTake(semaphore, portMAX_DELAY);

        // 5.18.3 Special Handling of ATA Read and Identify Drive Commands
        tmp = ide->read(IDE::Register::CylinderLow);
        if(tmp.low == 0x14) {
            tmp = ide->read(IDE::Register::CylinderHigh);
            if(tmp.low == 0xEB) rslt = true;
        }

        if(!rslt) ESP_LOGW(LOG_TAG, "Not an ATAPI device or device faulty!");

        xSemaphoreGive(semaphore);
        return rslt;
    }

    void Device::init_task_file() {
        xSemaphoreTake(semaphore, portMAX_DELAY);
        ESP_LOGI(LOG_TAG, "init task file");
        ide->write(IDE::Register::Feature, {{ .low = (FeatureRegister {{ .DMA = false, .overlap = false }}).value, .high = 0xFF }});

        // PIO buffer = 0x200
        ide->write(IDE::Register::CylinderHigh, {{ .low = 0x02, .high = 0xFF }});
        ide->write(IDE::Register::CylinderLow, {{ .low = 0x00, .high = 0xFF }});

        // Disable interrupts
        ide->write(IDE::Register::DeviceControl, {{ .low = (DeviceControlRegister {{ .nIEN = true }}).value, .high = 0xFF }});

        wait_not_busy();
        wait_drq_end();
        xSemaphoreGive(semaphore);
    }

    bool Device::self_test() {
        xSemaphoreTake(semaphore, portMAX_DELAY);
        ESP_LOGI(LOG_TAG, "Self-test");
        ide->write(IDE::Register::Command, {{ .low = Command::EXECUTE_DEVICE_DIAGNOSTIC, .high = 0xFF }});
        wait_not_busy();
        data16 rslt = ide->read(IDE::Register::Error);
        ESP_LOGI(LOG_TAG, "Error register = 0x%04x", rslt.value);
        xSemaphoreGive(semaphore);
        return rslt.low == 0x01;
    }

    void Device::identify() {
        Responses::IdentifyPacket rslt;
        memset(&rslt, 0, sizeof(Responses::IdentifyPacket));
        xSemaphoreTake(semaphore, portMAX_DELAY);

        ide->write(IDE::Register::Command, {{ .low = Command::IDENTIFY_PACKET_DEVICE, .high = 0xFF }});
        wait_not_busy();
        wait_drq();
        read_response(&rslt, sizeof(Responses::IdentifyPacket), true);
        xSemaphoreGive(semaphore);

        char buf[41] = { 0 };
        strncpy(buf, rslt.model, 40);
        ata_str_to_human(buf, 41);
        info.model = std::string(buf);

        strncpy(buf, rslt.firmware_rev, 40);
        ata_str_to_human(buf, 41);
        info.firmware = std::string(buf);

        strncpy(buf, rslt.serial_no, 40);
        ata_str_to_human(buf, 41);
        info.serial = std::string(buf);

        packet_size = ((rslt.general_config & 0x1) != 0) ? 16 : 12;
        
        quirks = { 0 };
        if(info.model == "NEC                 CD-ROM DRIVE:284" && info.firmware == "3.51    NEC                 CD-ROM DRIVE") {
            ESP_LOGW(LOG_TAG, "Shitty drive detected! NEC CDR-1400C anyone?");

            quirks.busy_ass = true;
            quirks.no_media_codes = true;
        }
        else if(info.model == "TEAC DV-W58G-A" || info.model == "PLDS    DVD A  DH20A4P") {
            ESP_LOGW(LOG_TAG, "Detected a drive that doesn't support hardware ffwd/rewind");
            quirks.must_use_softscan = true;
        }
        else if(info.model == "HL-DT-ST DVDRAM GSA-4163B") {
            // responses still make zero sense. UNSUPPORTED!
            quirks.no_media_codes = true;
            quirks.busy_ass = true;
            quirks.no_drq_in_toc = true;
        }
        else if(info.model == "TEAC CD-C68E") {
            quirks.fucky_toc_reads = true;
        }

        ESP_LOGI(LOG_TAG, "Drive Model = '%s', SN = '%s', FW = '%s', packet size = %i", info.model.c_str(),  info.serial.c_str(), info.firmware.c_str(), packet_size);
    }

    void Device::start(bool state) {
        const Requests::StartStopUnit req = {
            .opcode = OperationCodes::START_STOP_UNIT,
            .start = state,
            .load_eject = false
        };
        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&req, sizeof(req));
        xSemaphoreGive(semaphore);
    }

    void Device::eject(bool open) {
        const Requests::StartStopUnit req = {
            .opcode = OperationCodes::START_STOP_UNIT,
            .start = !open,
            .load_eject = true
        };
        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&req, sizeof(req));
        xSemaphoreGive(semaphore);
    }

    void Device::load_unload(SlotNumber slot) {
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

    void Device::stop() {
        const Requests::StopPlayScan req = {
            .opcode = OperationCodes::STOP_PLAY_SCAN
        };

        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&req, sizeof(req), true);
        xSemaphoreGive(semaphore);
    }

    void Device::pause(bool pause) {
        const Requests::PauseResume req = {
            .opcode = OperationCodes::PAUSE_RESUME,
            .resume = !pause
        };
        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&req, sizeof(req), true);
        xSemaphoreGive(semaphore);
    }

    void Device::play(const MSF start, const MSF end) {
        Requests::PlayAudioMSF req = {
            .opcode = OperationCodes::PLAY_AUDIO_MSF,
            .start_position = start,
            .end_position = end,
        };
        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&req, sizeof(req), true);
        xSemaphoreGive(semaphore);
    }

    void Device::scan(bool forward, const MSF from) {
        Requests::Scan req = {
            .opcode = OperationCodes::SCAN,
            .direct = !forward,
            .msf = from,
            .addr_type = Requests::Scan::ScanAddrType::ScanAddr_AbsMSF
        };
        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&req, sizeof(req), false);
        xSemaphoreGive(semaphore);
    }

    const std::vector<uint8_t> Device::read_cd_text_toc_locked() {
        // just for fun let's try this
        const Requests::ReadTOC req = {
            .opcode = OperationCodes::READ_TOC_PMA_ATIP,
            .msf = true,
            .format = TocFormat::TOC_FMT_CD_TEXT,
            .allocation_length = htobe16(0x7FFF) // oughtta be enough!
        };

        send_packet(&req, sizeof(req), true);
        delay(50); // some drives e.g. CD68E seem to be kinda slow on the response, producing invalid output
        wait_not_busy();

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
        }

        return rslt;
    }

    const DiscTOC Device::read_toc() {
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
            wait_not_busy();

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

    MediaTypeCode Device::check_media() {
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

    void Device::send_packet(const void * buf, size_t bufLen, bool pad) {
        delayMicroseconds(100); // either just the C68E is too old and slow to keep up with the ESP even over 100kHz i2c... or my pcb layout bites me in the ass again!!
        // for our use case those occasional delays don't have too much effect but this will limit potential growth for data disc reading if any!


        // Need to set nIEN before sending the PACKET command
        ide->write(IDE::Register::DeviceControl, {{ .low = (DeviceControlRegister {{ .nIEN = true }}).value, .high = 0xFF }});
        delayMicroseconds(100);
        ide->write(IDE::Register::Command, {{ .low = Command::WRITE_PACKET, .high = 0xFF }});

        const uint8_t* data = (const uint8_t*) buf;
        int i = 0;
        for(i = 0; i < bufLen; i+=2) {
            delayMicroseconds(10);
            ide->write(IDE::Register::Data, {{ .low = data[i], .high = data[i + 1] }});
        }

        if(pad) {
            for(; i < packet_size; i+=2) {
                delayMicroseconds(10);
                ide->write(IDE::Register::Data, { 0x00, 0x00 });
            }
        }

        if(quirks.busy_ass) {
            delay(5);
        }
        wait_not_busy();
    }

    bool Device::read_response(void * outBuf, size_t bufLen, bool flush) {
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
                buf[i++] = val.high;
                delayMicroseconds(10);

                sts = read_sts_regi();
                delayMicroseconds(10);
            } while(bufLen > i && (sts.DRQ || quirks.no_drq_in_toc));

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
            int flushed = 0;
            sts = read_sts_regi();
            while(sts.DRQ) {
                val = ide->read(IDE::Register::Data);
                flushed += 2;

                sts = read_sts_regi();
            }
            if(flushed > 0) ESP_LOGD(LOG_TAG, "Flushed %i extra bytes", flushed);
            rslt = true;
        }

        return rslt;
    }

    Device::StatusRegister Device::read_sts_regi() {
        StatusRegister val;
        val.value = ide->read(IDE::Register::Status).low;
        return val;
    }

    void Device::wait_sts_bit_set(StatusRegister bits) {
        TickType_t start_wait = xTaskGetTickCount();
        ESP_LOGD(LOG_TAG, "Wait for bit set 0x%02x", bits.value);

        do {
            if(xTaskGetTickCount() - start_wait >= pdMS_TO_TICKS(10000)) {
                ESP_LOGW(LOG_TAG, "Still waiting for bit set 0x%02x", bits.value);
                start_wait = xTaskGetTickCount();
            }
        } while ((read_sts_regi().value & bits.value) == 0);
    }

    void Device::wait_sts_bit_clr(StatusRegister bits) {
        TickType_t start_wait = xTaskGetTickCount();
        ESP_LOGD(LOG_TAG, "Wait for bit clear 0x%02x", bits.value);

        do {
            delay(100);
            if(xTaskGetTickCount() - start_wait >= pdMS_TO_TICKS(10000)) {
                ESP_LOGW(LOG_TAG, "Still waiting for bit clear 0x%02x", bits.value);
                start_wait = xTaskGetTickCount();
            }
        } while ((read_sts_regi().value & bits.value) != 0);
    }

    void Device::wait_not_busy() { 
        ESP_LOGV(LOG_TAG, "Waiting for drive to stop being busy...");
        wait_sts_bit_clr({{.BSY = true}}); 
    }
    void Device::wait_drq_end() { wait_sts_bit_clr({{.DRQ = true}}); }
    void Device::wait_drq() { wait_sts_bit_set({{.DRQ = true}}); }

    void Device::wait_ready() {
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