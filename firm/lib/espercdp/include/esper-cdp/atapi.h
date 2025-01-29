#pragma once
#include <esper-core/ide.h>
#include <string>
#include <vector>

#define MAX_CHANGER_SLOTS 10

namespace ATAPI {
    struct DriveInfo {
        std::string model;
        std::string serial;
        std::string firmware;
    };

    struct MechInfo {
        enum class ChangerState {
            Idle,
            Preparing,
            Changing_Disc
        };

        struct ChangerSlotState {
            bool disc_in;
            bool disc_changed;
        };

        uint8_t disc_count;
        bool is_door_open;
        bool is_playing;
        bool is_fault;

        uint8_t current_disc;
        ChangerState changer_state;
        ChangerSlotState changer_slots[MAX_CHANGER_SLOTS];
    };

    class Device {
    public:
        Device(Platform::IDE * bus);

        void reset();
        bool check_atapi_compatible();
        bool self_test();

        void wait_ready();
        void eject();

        const DriveInfo * get_info();
        const MechInfo * query_state();

    private:
        Platform::IDE * ide;
        DriveInfo info = { 
            .model = "", .serial = "", .firmware = ""
        };
        MechInfo mech_sts = { 0 };
        int packet_size = 12;

        union StatusRegister {
            struct __attribute__((packed)) {
                /// @brief Error
                bool ERR: 1;
                /// @brief Index Mark
                bool IDX: 1;
                /// @brief Data Corrected
                bool CORR: 1;
                /// @brief Transfer Requested
                bool DRQ: 1;
                /// @brief Seek Complete
                bool DSC: 1;
                /// @brief Device Fault
                bool DF: 1;
                /// @brief Device Ready
                bool DRDY: 1;
                /// @brief Busy
                bool BSY: 1;
            };
            uint8_t value;
        };

        union ErrorRegister {
            struct __attribute__((packed)) {
                /// @brief Address Mark Not Found
                bool AMNF: 1;
                /// @brief Track 0 Not Found
                bool TK0NF: 1;
                /// @brief Command Aborted
                bool ABRT: 1;
                /// @brief Media Change Requested
                bool MCR: 1;
                /// @brief ID Mark Not Found
                bool IDNF: 1;
                /// @brief Media Changed
                bool MC: 1;
                /// @brief Uncorrectable Data Error
                bool UNC: 1;
                /// @brief Bad Block
                bool BBK: 1;
            };
            uint8_t value;
        };

        union FeatureRegister {
            struct __attribute__((packed)) {
                bool DMA: 1;
                bool overlap: 1;
            };
            uint8_t value;
        };

        union DeviceControlRegister {
            struct __attribute__((packed)) {
                bool reserved0: 1;
                bool nIEN: 1;
                bool SRST: 1;
                bool reserved3: 1;
                bool reserved4: 1;
                bool reserved5: 1;
                bool reserved6: 1;
                bool HOB: 1;
            };
            uint8_t value;
        };


        void wait_sts_bit_set(StatusRegister bits);
        void wait_sts_bit_clr(StatusRegister bits);
        void read_response(void * buf, size_t bufLen, bool flush);
        void send_packet(const void * buf, size_t bufLen, bool pad = true);

        void wait_not_busy();
        void wait_drq_end();
        void wait_drq();

        void init_task_file();
        void identify();
    };
}