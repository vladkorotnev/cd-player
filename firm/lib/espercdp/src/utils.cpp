#include <esper-cdp/types.h>

namespace ATAPI {
    bool MediaTypeCodeIsAudioCD(MediaTypeCode code) {
        return code == MTC_AUDIO_120MM || code == MTC_AUDIO_80MM || code == MTC_AUDIO_DATA_120MM || code == MTC_AUDIO_DATA_80MM ||
            code == MTC_CDR_AUDIO_120MM || code == MTC_CDR_AUDIO_80MM || code == MTC_CDR_AUDIO_DATA_120MM || code == MTC_CDR_AUDIO_DATA_80MM ||
            code == MTC_CD_EXT_AUDIO_120MM || code == MTC_CD_EXT_AUDIO_80MM || code == MTC_CD_EXT_AUDIO_DATA_120MM || code == MTC_CD_EXT_AUDIO_DATA_80MM;
    }
};