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
            if(isspace(atastr[i])) atastr[i] = 0;
        }
    }

    Device::Device(Platform::IDE * bus): 
        ide(bus) 
    {
        // compile time sanity checks
        const StatusRegister tmp = StatusRegister { .value = (1 << 7) };
        assert(tmp.BSY);
    }

    void Device::reset() {
        ESP_LOGI(LOG_TAG, "Reset");
        ide->reset();
        delay(3000);
        wait_not_busy();
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
            .allocation_length = htobe16(sizeof(hdr))
        };

        send_packet(&req, sizeof(req));
        wait_not_busy();
        read_response(&hdr, sizeof(hdr), true);

        hdr.slot_tbl_len = be16toh(hdr.slot_tbl_len);

        ESP_LOGI(LOG_TAG, "Slot = %i, ChgSts = %i, Fault = %i, Door = %i, MechSts = %i, NumSlots = %i, SlotTblLen = %i, LBAPos = %02x%02x%02x", hdr.current_slot, hdr.changer_state, hdr.fault, hdr.door_open, hdr.mechanism_state, hdr.num_slots, hdr.slot_tbl_len, hdr.current_lba[0], hdr.current_lba[1], hdr.current_lba[2]);

        mech_sts.disc_count = std::max((uint8_t)1, (uint8_t)hdr.num_slots);
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
        } else {
            mech_sts.changer_state = MechInfo::ChangerState::Idle;
        }

        return &mech_sts;
    }

    bool Device::check_atapi_compatible() {
        data16 tmp;

        // 5.18.3 Special Handling of ATA Read and Identify Drive Commands
        tmp = ide->read(Register::CylinderLow);
        if(tmp.low == 0x14) {
            tmp = ide->read(Register::CylinderHigh);
            if(tmp.low == 0xEB) return true;
        }

        ESP_LOGW(LOG_TAG, "Not an ATAPI device or device faulty!");
        return false;
    }

    void Device::init_task_file() {
        ESP_LOGI(LOG_TAG, "init task file");
        ide->write(Register::Feature, {{ .low = (FeatureRegister {{ .DMA = false, .overlap = false }}).value, .high = 0xFF }});

        // PIO buffer = 0x200
        ide->write(Register::CylinderHigh, {{ .low = 0x02, .high = 0xFF }});
        ide->write(Register::CylinderLow, {{ .low = 0x00, .high = 0xFF }});

        // Disable interrupts
        ide->write(Register::DeviceControl, {{ .low = (DeviceControlRegister {{ .nIEN = true }}).value, .high = 0xFF }});

        wait_not_busy();
        wait_drq_end();
    }

    bool Device::self_test() {
        ESP_LOGI(LOG_TAG, "Self-test");
        ide->write(Register::Command, {{ .low = Command::EXECUTE_DEVICE_DIAGNOSTIC, .high = 0xFF }});
        wait_not_busy();
        data16 rslt = ide->read(Register::Error);
        ESP_LOGI(LOG_TAG, "Error register = 0x%04x", rslt.value);
        return rslt.low == 0x01;
    }

    void Device::identify() {
        Responses::IdentifyPacket rslt;
        memset(&rslt, 0, sizeof(Responses::IdentifyPacket));

        ide->write(Register::Command, {{ .low = Command::IDENTIFY_PACKET_DEVICE, .high = 0xFF }});
        wait_not_busy();
        wait_drq();
        read_response(&rslt, sizeof(Responses::IdentifyPacket), true);

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
        ESP_LOGI(LOG_TAG, "Drive Model = '%s', packet size = %i", info.model.c_str(), packet_size);
    }

    void Device::eject() {
        const Requests::StartStopUnit req = {
            .opcode = OperationCodes::START_STOP_UNIT,
            .start = false,
            .load_eject = true
        };

      const Requests::LoadUnload lul = {
            .opcode = OperationCodes::LOAD_UNLOAD,
            .start = true,
            .load_unload = true,
            .slot = 2
        };

        send_packet(&lul, sizeof(lul), false);

        // play audio
        char play[9] = { 0 };
        play[0] = 0x47;
        play[4] = 0x02;
        play[7] = 55;
        send_packet(&play, sizeof(play));

        // send_packet(&req, sizeof(req));
    }

    void Device::send_packet(const void * buf, size_t bufLen, bool pad) {
        // Need to set nIEN before sending the PACKET command
        ide->write(Register::DeviceControl, {{ .low = (DeviceControlRegister {{ .nIEN = true }}).value, .high = 0xFF }});
        ide->write(Register::Command, {{ .low = Command::WRITE_PACKET, .high = 0xFF }});

        const uint8_t* data = (const uint8_t*) buf;
        int i = 0;
        for(i = 0; i < bufLen; i+=2) {
            ide->write(Register::Data, {{ .low = data[i], .high = data[i + 1] }});
        }

        if(pad) {
            for(; i < packet_size; i+=2) {
                ide->write(Register::Data, { 0x00, 0x00 });
            }
        }

        wait_not_busy();
    }

    void Device::read_response(void * outBuf, size_t bufLen, bool flush) {
        if((bufLen == 0 || outBuf == nullptr) && !flush) {
            ESP_LOGE(LOG_TAG, "No buffer provided for response!");
            return;
        }

        data16 val;
        StatusRegister sts;
        uint8_t * buf = (uint8_t*)outBuf;

        if(bufLen > 0 && buf != nullptr) {
            int i = 0;
            do {
                val = ide->read(Register::Data);
                buf[i++] = val.low;
                buf[i++] = val.high;

                val = ide->read(Register::Status);
                sts.value = val.low;
            } while(bufLen > i && sts.DRQ);

            if(bufLen > i) {
                ESP_LOGW(LOG_TAG, "Buffer underrun when reading response: wanted %i bytes, DRQ clear after %i bytes", bufLen, i + 1);
            }
            else if(sts.DRQ && !flush) {
                ESP_LOGW(LOG_TAG, "Buffer overrun when reading response: wanted %i bytes, but DRQ still set", bufLen);
            }
        }

        if(flush) {
            int flushed = 0;
            do {
                val = ide->read(Register::Data);
                flushed += 2;

                val = ide->read(Register::Status);
                sts.value = val.low;
            } while(sts.DRQ);
            ESP_LOGI(LOG_TAG, "Flushed %i extra bytes", flushed);
        }
    }

    void Device::wait_sts_bit_set(StatusRegister bits) {
        data16 tmp;
        TickType_t start_wait = xTaskGetTickCount();
        ESP_LOGV(LOG_TAG, "Wait for bit set 0x%02x", bits.value);

        do {
            tmp = ide->read(Register::Status);
            if(xTaskGetTickCount() - start_wait >= pdMS_TO_TICKS(10000)) {
                ESP_LOGW(LOG_TAG, "Still waiting for bit set 0x%02x", bits.value);
                start_wait = xTaskGetTickCount();
            }
        } while ((tmp.low & bits.value) == 0);
    }

    void Device::wait_sts_bit_clr(StatusRegister bits) {
        data16 tmp;
        TickType_t start_wait = xTaskGetTickCount();
        ESP_LOGV(LOG_TAG, "Wait for bit clear 0x%02x", bits.value);

        do {
            tmp = ide->read(Register::Status);
            if(xTaskGetTickCount() - start_wait >= pdMS_TO_TICKS(10000)) {
                ESP_LOGW(LOG_TAG, "Still waiting for bit clear 0x%02x", bits.value);
                start_wait = xTaskGetTickCount();
            }
        } while ((tmp.low & bits.value) != 0);
    }

    void Device::wait_not_busy() { 
        ESP_LOGV(LOG_TAG, "Waiting for drive to stop being busy...");
        wait_sts_bit_clr({{.BSY = true}}); 
    }
    void Device::wait_drq_end() { wait_sts_bit_clr({{.DRQ = true}}); }
    void Device::wait_drq() { wait_sts_bit_set({{.DRQ = true}}); }
    void Device::wait_ready() {
        ESP_LOGI(LOG_TAG, "Waiting for drive to become ready...");
        wait_sts_bit_set({{.DRDY = true}}); 
    }
}