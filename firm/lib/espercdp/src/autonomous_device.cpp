#include <esper-cdp/autonomous_device.h>
#include <esper-cdp/atapi-protocol.h>
#include <endian.h>

static const char LOG_TAG[] = "ATAPI";

namespace ATAPI {
    void AutonomousPlayerDevice::stop() {
        const Requests::StopPlayScan req = {
            .opcode = OperationCodes::STOP_PLAY_SCAN
        };

        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&req, sizeof(req), true);
        xSemaphoreGive(semaphore);
    }

    void AutonomousPlayerDevice::pause(bool pause) {
        const Requests::PauseResume req = {
            .opcode = OperationCodes::PAUSE_RESUME,
            .resume = !pause
        };
        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&req, sizeof(req), true);
        xSemaphoreGive(semaphore);
    }

    void AutonomousPlayerDevice::mode_select_output_ports() {
        const Requests::ModeSense msrq = {
            .opcode = OperationCodes::MODE_SENSE,
            .page = ModeSensePageCode::MSPC_CDA_CONTROL,
            .allocation_length = htobe16(0xFF)
        };

        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&msrq, sizeof(msrq), true);
        Responses::ModeSense mph;
        ModeSenseCDAControlModePage cdactl;
        read_response(&mph, sizeof(mph), false);
        read_response(&cdactl, sizeof(cdactl), true);
        xSemaphoreGive(semaphore);

        if(cdactl.page_code != ModeSensePageCode::MSPC_CDA_CONTROL) {
            ESP_LOGE(LOG_TAG, "Expected mode sense page code 0x%02x, but got 0x%02x ???", ModeSensePageCode::MSPC_CDA_CONTROL, cdactl.page_code);
            return;
        }

        _diags.cda = cdactl;

        ESP_LOGW(LOG_TAG, "Before CDA CTL: port[0] = {%i, %i}, port[1] = {%i, %i}, port[2] = {%i, %i}, port[3] = {%i, %i}", 
            cdactl.ports[0].channel, cdactl.ports[0].volume, cdactl.ports[1].channel, cdactl.ports[1].volume, cdactl.ports[2].channel, cdactl.ports[2].volume, cdactl.ports[3].channel, cdactl.ports[3].volume);

        cdactl.ports[0].channel = 1;
        cdactl.ports[0].volume = 255;
        cdactl.ports[1].channel = 2;
        cdactl.ports[1].volume = 255;
        cdactl.ports[2].channel = 1;
        cdactl.ports[2].volume = 255;
        cdactl.ports[3].channel = 2;
        cdactl.ports[3].volume = 255;
        mph.media_type = MediaTypeCode::MTC_DOOR_CLOSED_UNKNOWN;
        mph.block_descriptor_length_0 = 0;
        mph.data_length = 0; // When using the MODE SENSE command, the mode data length field specifies the length in bytes of the following data that is available to be transferred. The mode data length is the total byte count of all data following the mode data length field. When using the MODE SELECT command, this field is reserved.

        ESP_LOGV(LOG_TAG, "SEND CDA CTL: port[0] = {%i, %i}, port[1] = {%i, %i}, port[2] = {%i, %i}, port[3] = {%i, %i}", 
            cdactl.ports[0].channel, cdactl.ports[0].volume, cdactl.ports[1].channel, cdactl.ports[1].volume, cdactl.ports[2].channel, cdactl.ports[2].volume, cdactl.ports[3].channel, cdactl.ports[3].volume);

        Requests::ModeSelect msel = {
            .opcode = OperationCodes::MODE_SELECT,
            .set_me_to_true = true,
            .parameter_list_length = htobe16(sizeof(mph) + sizeof(cdactl))
        };

        struct ATAPI_PKT {
            Requests::ModeSelect a = {
                .opcode = OperationCodes::MODE_SELECT,
                .set_me_to_true = true,
                .parameter_list_length = htobe16(sizeof(mph) + sizeof(cdactl))
            };
            Responses::ModeSense b;
            ModeSenseCDAControlModePage c;
        } tmp;
        tmp.b = mph;
        tmp.c = cdactl;

        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&tmp, sizeof(tmp), true);
        Responses::ModeSense mph2;
        ModeSenseCDAControlModePage cdactl2;
        send_packet(&msrq, sizeof(msrq), true);
        read_response(&mph2, sizeof(mph2), false);
        read_response(&cdactl2, sizeof(cdactl2), true);
        xSemaphoreGive(semaphore);

        if(cdactl2.page_code != ModeSensePageCode::MSPC_CDA_CONTROL) {
            ESP_LOGE(LOG_TAG, "Expected mode sense 2 page code 0x%02x, but got 0x%02x ???", ModeSensePageCode::MSPC_CDA_CONTROL, cdactl2.page_code);
            return;
        }

        ESP_LOGW(LOG_TAG, "After CDA CTL: port[0] = {%i, %i}, port[1] = {%i, %i}, port[2] = {%i, %i}, port[3] = {%i, %i}", 
            cdactl2.ports[0].channel, cdactl2.ports[0].volume, cdactl2.ports[1].channel, cdactl2.ports[1].volume, cdactl2.ports[2].channel, cdactl2.ports[2].volume, cdactl2.ports[3].channel, cdactl2.ports[3].volume);
        
        _diags.cda = cdactl2;
    }

    void AutonomousPlayerDevice::mode_select_power_conditions() {
        // disable standby timers (e.g. some TEAC drives go to sleep during long pause)
        const Requests::ModeSense msrq = {
            .opcode = OperationCodes::MODE_SENSE,
            .page = ModeSensePageCode::MSPC_POWER_CONDITION,
            .allocation_length = htobe16(0xFF)
        };

        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&msrq, sizeof(msrq), true);
        Responses::ModeSense mph;
        Responses::ModeSensePowerConditionModePage pwrctl;
        read_response(&mph, sizeof(mph), false);
        read_response(&pwrctl, sizeof(pwrctl), true);
        xSemaphoreGive(semaphore);

        if(pwrctl.page_code != ModeSensePageCode::MSPC_POWER_CONDITION) {
            ESP_LOGE(LOG_TAG, "Expected mode sense page code 0x%02x, but got 0x%02x ???", ModeSensePageCode::MSPC_POWER_CONDITION, pwrctl.page_code);
            return;
        }

        pwrctl.idle_timer = be16toh(pwrctl.idle_timer);
        pwrctl.standby_timer = be16toh(pwrctl.standby_timer);

        ESP_LOGI(LOG_TAG, "Initial power status: IDLE[%i: %i] STBY[%i: %i]", pwrctl.idle, pwrctl.idle_timer, pwrctl.standby, pwrctl.standby_timer);

        pwrctl.idle = 0;
        pwrctl.standby = 0;
        pwrctl.idle_timer = 0xFFFF;
        pwrctl.standby_timer = 0xFFFF;

        struct ATAPI_PKT {
            Requests::ModeSelect a = {
                .opcode = OperationCodes::MODE_SELECT,
                .set_me_to_true = true,
                .parameter_list_length = htobe16(sizeof(mph) + sizeof(pwrctl))
            };
            Responses::ModeSense b;
            Responses::ModeSensePowerConditionModePage c;
        } tmp;
        tmp.b = mph;
        tmp.c = pwrctl;

        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&tmp, sizeof(tmp), true);
        Responses::ModeSense mph2;
        Responses::ModeSensePowerConditionModePage pwrctl2;
        send_packet(&msrq, sizeof(msrq), true);
        read_response(&mph2, sizeof(mph2), false);
        read_response(&pwrctl2, sizeof(pwrctl2), true);
        xSemaphoreGive(semaphore);

        if(pwrctl2.page_code != ModeSensePageCode::MSPC_POWER_CONDITION) {
            ESP_LOGE(LOG_TAG, "Expected mode sense 2 page code 0x%02x, but got 0x%02x ???", ModeSensePageCode::MSPC_POWER_CONDITION, pwrctl2.page_code);
            return;
        }
        pwrctl2.idle_timer = be16toh(pwrctl.idle_timer);
        pwrctl2.standby_timer = be16toh(pwrctl.standby_timer);
        ESP_LOGI(LOG_TAG, "NEW power status: IDLE[%i: %i] STBY[%i: %i]", pwrctl2.idle, pwrctl2.idle_timer, pwrctl2.standby, pwrctl2.standby_timer);
    }

    void AutonomousPlayerDevice::play(const MSF start, const MSF end) {
        if(!playback_mode_select_flag) {
            mode_select_output_ports();
            mode_select_power_conditions();
            playback_mode_select_flag = true;
        }
        Requests::PlayAudioMSF req = {
            .opcode = OperationCodes::PLAY_AUDIO_MSF,
            .start_position = start,
            .end_position = end,
        };
        xSemaphoreTake(semaphore, portMAX_DELAY);
        send_packet(&req, sizeof(req), true);
        xSemaphoreGive(semaphore);
    }

    void AutonomousPlayerDevice::scan(bool forward, const MSF from) {
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

    const AudioStatus * AutonomousPlayerDevice::query_position() {
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

    bool AutonomousPlayerDevice::initialize() {
        bool rslt = HardwareATAPIDevice::initialize();
        _diags.is_autonomous = true;
        return rslt;
    }
}
