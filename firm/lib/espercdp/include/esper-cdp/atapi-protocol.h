#pragma once
#include "types.h"

#define ATAPI_PKT __attribute__((packed))

namespace ATAPI {
    const uint8_t TRK_NUM_LEAD_OUT = 0xAA;
    enum Command: uint8_t {
        EXECUTE_DEVICE_DIAGNOSTIC = 0x90,
        WRITE_PACKET = 0xA0,
        IDENTIFY_PACKET_DEVICE = 0xA1
    };
    
    enum OperationCodes: uint8_t{
        TEST_UNIT_READY = 0x00,
        REQUEST_SENSE = 0x03,
        START_STOP_UNIT = 0x1B,
        PREVENT_ALLOW_MEDIA_REMOVAL = 0x1E,
        READ_SUBCHANNEL = 0x42,
        READ_TOC_PMA_ATIP = 0x43,
        PLAY_AUDIO_MSF = 0x47,
        PAUSE_RESUME = 0x4B,
        STOP_PLAY_SCAN = 0x4E,
        MODE_SELECT = 0x55,
        MODE_SENSE = 0x5A,
        LOAD_UNLOAD = 0xA6,
        MECHANISM_STATUS = 0xBD,
        SCAN = 0xBA,
        PLAY_CD = 0xBC,
        READ_CD = 0xBE,
    };

    enum SubchannelFormat: uint8_t {
        SUBCH_FMT_CD_POS = 0x01,
        SUBCH_FMT_UPC_BARCODE = 0x02,
        SUBCH_FMT_ISRC = 0x03,
        // others reserved
    };

    enum TocFormat: uint8_t {
        TOC_FMT_TOC = 0,
        TOC_FMT_SESSION_INFO = 1,
        TOC_FMT_FULL_TOC = 2, 
        TOC_FMT_PMA = 3,
        TOC_FMT_ATIP = 4,
        TOC_FMT_CD_TEXT = 5
    };

    enum SubchannelAdr: uint8_t {
        SUBCH_ADR_NO_Q_DATA = 0,
        SUBCH_ADR_CUR_POS_DATA = 1,
        SUBCH_ADR_CAT_NO = 2,
        SUBCH_ADR_ISRC = 3
    };

    enum ModeSensePageCode: uint8_t {
        MSPC_VENDOR = 0,
        MSPC_ERROR_RECOVERY = 1,
        MSPC_CDA_CONTROL = 0xE,
        MSPC_FEATURE_SUPPORT = 0x18,
        MSPC_POWER_CONDITION = 0x1A,
        MSPC_FAILURE_REPORT = 0x1C,
        MSPC_INACTIVITY = 0x1D,
        MSPC_CAPABILITIES_MECH_STS = 0x2A
    };

    enum RequestSenseKey: uint8_t {
        SENSE_NOT_READY = 0x06
        // YAGNI
    };

    enum RequestSenseAsc: uint8_t {
        ASC_DEVICE_NOT_READY = 0x29
        // YAGNI
    };

    namespace Requests {
        struct ATAPI_PKT ReqPktFooter {
            bool link: 1;
            bool flag: 1;
            bool NACA: 1;
            uint8_t reserved2: 3;
            uint8_t vendor_specific: 2;
        };

        struct ATAPI_PKT ReadSubchannel {
            uint8_t opcode;
            bool reserved0: 1;
            bool msf: 1;
            uint8_t reserved1: 3;
            uint8_t lun: 3;
            uint8_t reserved2: 6;
            bool subQ: 1;
            bool reserved3: 1;
            SubchannelFormat data_format;
            uint8_t reserved4[2];
            uint8_t track_no_for_isrc;
            uint16_t allocation_length;
            ReqPktFooter footer;
        };

        struct ATAPI_PKT PlayAudioMSF {
            uint8_t opcode;
            uint8_t reserved: 5;
            uint8_t lun: 3;
            uint8_t reserved1;
            MSF start_position;
            MSF end_position;
            ReqPktFooter footer;
        };

        struct ATAPI_PKT PlayCD {
            uint8_t opcode;

            bool reserved0: 1;
            bool is_msf: 1;
            uint8_t expected_sector_type: 3;
            uint8_t lun: 3;

            uint8_t reserved1;
            MSF start_position;
            MSF end_position;
            uint8_t reserved2;

            bool audio: 1;
            bool composite: 1;
            bool port1: 1;
            bool port2: 1;
            uint8_t reserved3: 3;
            bool speed: 1;

            ReqPktFooter footer;
        };

        struct ATAPI_PKT PauseResume {
            uint8_t opcode;

            uint8_t reserved: 5;
            uint8_t lun: 3;

            uint8_t reserved1[6];
            
            bool resume: 1;
            uint8_t reserved2: 7;

            ReqPktFooter footer;
        };

        struct ATAPI_PKT StopPlayScan {
            uint8_t opcode;

            uint8_t reserved: 5;
            uint8_t lun: 3;
            uint8_t reserved1[7];
            ReqPktFooter footer;
        };

        struct ATAPI_PKT StartStopUnit {
            uint8_t opcode;
            
            bool immediate: 1;
            uint8_t reserved: 4;
            uint8_t lun: 3;

            uint8_t reserved0[2];

            bool start: 1;
            bool load_eject: 1;
            uint8_t reserved1: 2;
            uint8_t power_condition: 4;
            
            ReqPktFooter footer;
        };

        struct ATAPI_PKT LoadUnload {
            uint8_t opcode;
            
            bool immediate: 1;
            uint8_t reserved: 4;
            uint8_t lun: 3;

            uint8_t reserved0[2];

            bool start: 1;
            bool load_unload: 1;
            uint8_t reserved1: 6;
            uint8_t reserved2[3];
            uint8_t slot;
            uint8_t reserved3[2];
            
            ReqPktFooter footer;
        };

        struct ATAPI_PKT MechanismStatus {
            uint8_t opcode;

            uint8_t reserved0: 5;
            uint8_t lun: 3;

            uint8_t reserved1[6];

            uint16_t allocation_length;

            uint8_t reserved2;

            ReqPktFooter footer;
        };

        struct ATAPI_PKT ReadTOC {
            uint8_t opcode;
            bool reserved0: 1;
            bool msf: 1;
            uint8_t reserved1: 3;
            uint8_t lun: 3;

            TocFormat format: 4;
            uint8_t reserved2: 4;
            uint8_t reserved3[3];
            uint8_t track_session_no;
            uint16_t allocation_length;

            ReqPktFooter footer;
        };

        struct ATAPI_PKT ModeSense {
            enum PageControl {
                PCTL_CURRENT,
                PCTL_CHANGEABLE,
                PCTL_DEFAULT,
                PCTL_SAVED
            };
            uint8_t opcode;
            uint8_t reserved0: 3;
            bool dbd: 1;
            bool reserved1: 1;
            uint8_t lun: 3;

            ModeSensePageCode page: 6;
            PageControl page_control: 2;

            uint8_t reserved2[4];

            uint16_t allocation_length;

            ReqPktFooter footer;
        };

        struct ATAPI_PKT RequestSense {
            uint8_t opcode;
            uint8_t reserved0: 5;
            uint8_t lun: 3;
            uint8_t reserved1[2];
            uint8_t allocation_length;
            ReqPktFooter footer;
        };

        struct ATAPI_PKT PreventAllowMediaRemoval {
            uint8_t opcode;
            uint8_t reserved0: 5;
            uint8_t lun: 3;

            uint8_t reserved1[2];
            bool prevent: 1;
            bool persistent: 1;
            uint8_t reserved2: 6;
            
            ReqPktFooter footer;
        };

        struct ATAPI_PKT ReadCd {
            enum SectorType: uint8_t {
                SECT_TYPE_ANY = 0b000,
                SECT_TYPE_CDDA = 0b001,
                SECT_TYPE_MODE1 = 0b010,
                SECT_TYPE_MODE2 = 0b011,
                SECT_TYPE_MODE2FORM1 = 0b100,
                SECT_TYPE_MODE2FORM2 = 0b101,
            };

            enum SubchannelDataSelection {
                NO_SUBCHANNEL = 0b000,
                RAW_SUBCHANNEL = 0b001,
                SUBCHANNEL_Q = 0b010,
                SUBCHANNEL_R_W = 0b100
            };
            
            uint8_t opcode;

            bool rel_addr: 1;
            bool reserved0: 1;
            SectorType sector_type: 3;
            uint8_t lun: 3;
            union {
                struct {
                    uint16_t lba_hi;
                    uint16_t lba_low;
                };
                struct {
                    uint8_t padding0;
                    MSF msf; // is this valid here?
                };
            };
            struct {
                uint8_t high;
                uint16_t low;
            } transfer_length_blocks;

            bool reserved1: 1;
            uint8_t error_flags: 2;
            bool edc_ecc: 1;
            bool user: 1;
            uint8_t headers: 2;
            bool synch: 1;

            SubchannelDataSelection subchannel: 3;
        };

        struct ATAPI_PKT Scan {
            enum ScanAddrType: uint8_t {
                // ScanAddr_LBA = 0b00,
                ScanAddr_AbsMSF = 0b01,
                // ScanAddr_TrkNo = 0b10
            };

            uint8_t opcode;

            bool rel_adr: 1;
            uint8_t reserved0: 3;
            bool direct: 1;
            uint8_t lun: 3;

            uint8_t padding0;
            MSF msf;

            uint8_t reserved1[3];
            
            uint8_t reserved2: 6;
            ScanAddrType addr_type: 2;

            uint8_t reserved3;

            ReqPktFooter footer;
        };

        struct ATAPI_PKT ModeSelect {
            uint8_t opcode;
            bool persist: 1;
            uint8_t reserved0: 3;
            bool set_me_to_true: 1;
            uint8_t reserved1: 3;
            uint8_t reserved2[5];
            uint16_t parameter_list_length;
            uint8_t reserved[3];
        };
    }

    namespace Responses {
        struct ATAPI_PKT ReadSubchannel {
            enum SubchannelAudioStatus: uint8_t {
                AUDIOSTS_UNSUPPORTED_INVALID = 0,
                AUDIOSTS_PLAYING = 0x11,
                AUDIOSTS_PAUSED = 0x12,
                AUDIOSTS_COMPLETED = 0x13,
                AUDIOSTS_STOPPED_ERROR = 0x14,
                AUDIOSTS_NONE = 0x15
            };
    
            uint8_t reserved0;
            SubchannelAudioStatus audio_status;
            uint16_t data_length;
            SubchannelFormat data_format;
            union {
                struct __attribute__((packed)) {
                    bool pre_emphasis: 1;
                    bool copy_protected: 1;
                    bool data_track: 1;
                    bool quadrophonic: 1;
                    SubchannelAdr adr: 4;
                    uint8_t track_no;
                    uint8_t index_no;
                    uint8_t padding0;
                    MSF absolute_address;
                    uint8_t padding1;
                    MSF relative_address;
                } current_position_data;
                struct __attribute__((packed)) {
                    uint8_t reserved0[3];
                    uint8_t reserved1: 7;
                    bool mc_val: 1;
                    char upc[14];
                    uint8_t aframe;
                } upc_barcode_data;
                struct __attribute__((packed)) {
                    bool pre_emphasis: 1;
                    bool copy_protected: 1;
                    bool data_track: 1;
                    bool quadrophonic: 1;
                    SubchannelAdr adr: 4;
                    uint8_t track_no;
                    uint8_t reserved0;
                    char isrc[13];
                    uint8_t aframe;
                    uint8_t reserved1;
                } isrc_data;
            };
        };

        struct ATAPI_PKT IdentifyPacket {
            uint16_t general_config;
            uint16_t reserved1;
            uint16_t specific_config;
            uint16_t reserved2[7];
            char serial_no[20];
            uint16_t reserved3[3];
            char firmware_rev[8];
            char model[40];
            
            // ... we dont really care of the rest for now
        };

        struct ATAPI_PKT MechanismStatusHeader {
            enum ChangerState: uint8_t {
                Ready = 0, Loading = 1, Unloading = 2, Initializing = 3
            };

            enum MechState: uint8_t {
                Idle = 0, Audio = 1, Scan = 2, OtherActivity = 3, Unknown = 7
            };

            uint8_t current_slot: 5;
            ChangerState changer_state: 2;
            bool fault: 1;

            uint8_t reserved0: 4;
            bool door_open: 1;
            MechState mechanism_state: 3;

            uint8_t current_lba[3];

            uint8_t num_slots: 5;
            uint8_t reserved1: 3;

            uint16_t slot_tbl_len;
        };

        struct ATAPI_PKT MechanismStatusSlotTable {
            bool disc_changed: 1;
            uint8_t reserved0: 6;
            bool disc_present: 1;
            uint8_t reserved1[3];
        };

        struct ATAPI_PKT NormalTOCEntry {
            uint8_t reserved0;
            bool pre_emphasis: 1;
            bool copy_protected: 1;
            bool data_track: 1;
            bool quadrophonic: 1;
            SubchannelAdr adr: 4; 
            uint8_t track_no;
            uint8_t reserved1;
            union {
                struct {
                    uint8_t padding;
                    MSF address;
                };
                uint8_t lba[4];
            };
        };

        struct ATAPI_PKT ReadTOCResponseHeader {
            uint16_t data_length;
            uint8_t first_track_no;
            uint8_t last_track_no;

            // YAGNI the other kinds!
        };

        struct ATAPI_PKT ModeSense {
            uint16_t data_length;
            MediaTypeCode media_type; // <- Obsolete!
            uint8_t reserved[3];
            uint16_t block_descriptor_length_0;

            // YAGNI the rest!
        };

        struct ATAPI_PKT RequestSense {
            uint8_t error_code: 6;
            bool valid: 1;
            uint8_t segment_no;

            RequestSenseKey sense_key: 4;
            bool reserved0: 1;
            bool ili: 1;
            uint8_t reserved1: 2;

            uint16_t information;

            uint8_t additional_sense_length;
            uint8_t command_specific_information[4];
            RequestSenseAsc additional_sense_code;
            uint8_t additional_sense_code_qualifier;
            uint8_t field_replaceable_unit_code;

            // ... YAGNI the rest, as usual
        };

        struct ATAPI_PKT ModeSensePowerConditionModePage {
            uint8_t page_code: 6;
            bool: 1;
            bool saveable: 1;
            uint8_t page_length;
    
            uint8_t: 8;
    
            bool standby: 1;
            bool idle: 1;
            uint8_t: 6;
            uint32_t idle_timer;
            uint32_t standby_timer;
        };
    }
}