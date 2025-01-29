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
        START_STOP_UNIT = 0x1B
    };

    namespace Requests {
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
            
            bool link: 1;
            bool flag: 1;
            bool NACA: 1;
            uint8_t reserved2: 3;
            uint8_t vendor_specific: 2;
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
    }
}