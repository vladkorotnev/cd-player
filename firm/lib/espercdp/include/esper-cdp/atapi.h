#pragma once
#include <esper-core/ide.h>
#include "types.h"
#include <vector>

namespace ATAPI {
    class Device {
    public:
        Device(Platform::IDEBus * bus);

        void reset();
        bool check_atapi_compatible();
        bool self_test();

        void wait_ready();

        /// @brief Eject or close the tray
        /// @param open_tray true = open tray, false = close tray
        void eject(bool open_tray);
        /// @brief Request the changer (if any) to switch to a specified slot and put the disc in play position
        /// @note The changer might not move right away (e.g. TEAC CD-C68E does not). In this case maybe follow up the command with `eject(false)`.
        void load_unload(SlotNumber slot);

        /// @brief Request the player to play audio data
        /// @param start Start of the played segment. Set to beginning of first track (usually M00S02F00, but can vary disc by disc)
        /// @param end End of the played segment. Set to lead-out to play the whole track.
        void play(MSF start, MSF end);
        /// @brief Change the pause state of the player
        /// @param pause true = pause playback, false = continue playback
        void pause(bool pause);
        /// @brief Stop the audio playback
        void stop();
        /// @brief Check the media status
        MediaTypeCode check_media();

        const DiscTOC read_toc();

        const DriveInfo * get_info();
        const MechInfo * query_state();
        const AudioStatus * query_position();

    private:
        Platform::IDEBus * ide;
        DriveInfo info = { 
            .model = "", .serial = "", .firmware = ""
        };
        MechInfo mech_sts = { 0 };
        AudioStatus audio_sts = { 0 };
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

        StatusRegister read_sts_regi();
        void wait_sts_bit_set(StatusRegister bits);
        void wait_sts_bit_clr(StatusRegister bits);
        bool read_response(void * buf, size_t bufLen, bool flush);
        void send_packet(const void * buf, size_t bufLen, bool pad = true);

        void wait_not_busy();
        void wait_drq_end();
        void wait_drq();

        void init_task_file();
        void identify();
    };
}