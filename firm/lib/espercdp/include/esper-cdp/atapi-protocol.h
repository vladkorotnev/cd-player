#pragma once
#include <stdint.h>

namespace ATAPI {
    enum Register {
        Data = 0xF0,
        Error = 0xF1,
        Feature = Error,
        SectorCount = 0xF2,
        SectorNumber = 0xF3,
        CylinderLow = 0xF4,
        CylinderHigh = 0xF5,
        DriveSelect = 0xF6,
        Command = 0xF7,
        Status = Command,
        AlternateStatus = 0xEE,
        DeviceControl = AlternateStatus
    };

    enum Command {
        EXECUTE_DEVICE_DIAGNOSTIC = 0x90,
        WRITE_PACKET = 0xA0,
        IDENTIFY_PACKET_DEVICE = 0xA1
    };
    
    enum OperationCodes {
        START_STOP_UNIT = 0x1B,
        LOAD_UNLOAD = 0xA6,
        MECHANISM_STATUS = 0xBD,
    };

    namespace Requests {
        struct __attribute__((packed)) ReqPktFooter {
            bool link: 1;
            bool flag: 1;
            bool NACA: 1;
            uint8_t reserved2: 3;
            uint8_t vendor_specific: 2;
        };

        struct __attribute__((packed)) StartStopUnit {
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

        struct __attribute__((packed)) LoadUnload {
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


        struct __attribute__((packed)) MechanismStatus {
            uint8_t opcode;

            uint8_t reserved0: 5;
            uint8_t lun: 3;

            uint8_t reserved1[6];

            uint16_t allocation_length;

            uint8_t reserved2;

            ReqPktFooter footer;
        };
    }

    namespace Responses {
        struct __attribute__((packed)) IdentifyPacket {
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

        struct __attribute__((packed)) MechanismStatusHeader {
            enum ChangerState {
                Ready = 0, Loading = 1, Unloading = 2, Initializing = 3
            };

            enum MechState {
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

        struct __attribute__((packed)) MechanismStatusSlotTable {
            bool disc_changed: 1;
            uint8_t reserved0: 6;
            bool disc_present: 1;
            uint8_t reserved1[3];
        };
    }
}