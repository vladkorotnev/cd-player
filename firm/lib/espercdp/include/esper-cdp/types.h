#pragma once
#include <stdint.h>
#include <string>
#include <vector>

#define MAX_CHANGER_SLOTS 10

struct MSF {
    static const int FRAMES_IN_SECOND = 75;

    /// @brief Minutes, 0-99
    uint8_t M;
    /// @brief Seconds, 0-59
    uint8_t S;
    /// @brief Frames, 0-74
    uint8_t F;
};

#define MSF_TO_FRAMES(x) (x.F + (x.S + x.M * 60) * MSF::FRAMES_IN_SECOND)
#define MSF_TO_MILLIS(x) ((x.S + x.M * 60) * 1000 + x.F * (1000 / MSF::FRAMES_IN_SECOND)) // rough approximation
MSF FRAMES_TO_MSF(int frames);
MSF operator +(MSF a, MSF b);
MSF operator -(MSF a, MSF b);

namespace ATAPI {
     using SlotNumber = uint8_t;

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

        SlotNumber slot_count;
        bool is_door_open;
        bool is_playing;
        bool is_fault;
        bool is_in_motion;

        SlotNumber current_disc;
        ChangerState changer_state;
        ChangerSlotState changer_slots[MAX_CHANGER_SLOTS];
    };

    struct AudioStatus {
        enum class PlayState {
            Stopped,
            Playing,
            Paused
        };

        uint8_t track;
        uint8_t index;
        MSF position_in_disc;
        MSF position_in_track;
        PlayState state;
    };

    struct DiscTrack {
        uint8_t number;
        MSF position;
        bool is_data;
        bool preemphasis;
    };

    struct DiscTOC {
        const MSF leadOut;
        const std::vector<DiscTrack> tracks;
        const std::vector<uint8_t> toc_subchannel;
    };

    enum MediaTypeCode: uint8_t {
        MTC_DOOR_CLOSED_UNKNOWN = 0x0,
        
        MTC_DATA_120MM = 0x1,
        MTC_AUDIO_120MM = 0x2,
        MTC_AUDIO_DATA_120MM = 0x3,
        MTC_PHOTO_120MM = 0x4,

        MTC_DATA_80MM = 0x5,
        MTC_AUDIO_80MM = 0x6,
        MTC_AUDIO_DATA_80MM = 0x7,
        MTC_PHOTO_80MM = 0x8,

        MTC_CDR_UNKNOWN = 0x10,
        MTC_CDR_DATA_120MM = 0x11,
        MTC_CDR_AUDIO_120MM = 0x12,
        MTC_CDR_AUDIO_DATA_120MM = 0x13,
        MTC_CDR_PHOTO_120MM = 0x14,

        MTC_CDR_DATA_80MM = 0x15,
        MTC_CDR_AUDIO_80MM = 0x16,
        MTC_CDR_AUDIO_DATA_80MM = 0x17,
        MTC_CDR_PHOTO_80MM = 0x18,
        
        MTC_CD_EXT_UNKNOWN = 0x20,
        MTC_CD_EXT_DATA_120MM = 0x21,
        MTC_CD_EXT_AUDIO_120MM = 0x22,
        MTC_CD_EXT_AUDIO_DATA_120MM = 0x23,
        MTC_CD_EXT_PHOTO_120MM = 0x24,
        MTC_CD_EXT_DATA_80MM = 0x25,
        MTC_CD_EXT_AUDIO_80MM = 0x26,
        MTC_CD_EXT_AUDIO_DATA_80MM = 0x27,
        MTC_CD_EXT_PHOTO_80MM = 0x28,
        
        MTC_DOOR_CLOSED_UNKNOWN_ALT = 0x30,
        MTC_DOOR_CLOSED_120MM = 0x31,
        MTC_DOOR_CLOSED_80MM = 0x35,

        MTC_NO_DISC = 0x70,
        MTC_DOOR_OPEN = 0x71,
        MTC_FORMAT_ERROR = 0x72
    };

    bool MediaTypeCodeIsAudioCD(MediaTypeCode code);
}