#include <esper-cdp/types.h>

char *rfc822_binary(void *src,unsigned long srcl,size_t *len)
{
  char *ret,*d;
  char *s = (char *) src;
  const char *v = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._";
  unsigned long i = ((srcl + 2) / 3) * 4;
  *len = i += 2 * ((i / 60) + 1);
  d = ret = (char *) malloc ((size_t) ++i);
  for (i = 0; srcl; s += 3) {	/* process tuplets */
    *d++ = v[s[0] >> 2];	/* byte 1: high 6 bits (1) */
				/* byte 2: low 2 bits (1), high 4 bits (2) */
    *d++ = v[((s[0] << 4) + (--srcl ? (s[1] >> 4) : 0)) & 0x3f];
				/* byte 3: low 4 bits (2), high 2 bits (3) */
    *d++ = srcl ? v[((s[1] << 2) + (--srcl ? (s[2] >> 6) : 0)) & 0x3f] : '-';
				/* byte 4: low 6 bits (3) */
    *d++ = srcl ? v[s[2] & 0x3f] : '-';
    if (srcl) srcl--;		/* count third character if processed */
    if ((++i) == 15) {		/* output 60 characters? */
      i = 0;			/* restart line break count, insert CRLF */
      *d++ = '\015'; *d++ = '\012';
    }
  }
  *d = '\0';			/* tie off string */

  return ret;			/* return the resulting string */
}

const std::string urlEncode(const std::string& src) {
    const char *hex = "0123456789ABCDEF";
    std::string encodedMsg = "";
    encodedMsg.reserve(src.length() * 2);
    
    for(const char c: src) {
        if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            encodedMsg += c;
        } else if(' ' == c) {
            encodedMsg += '+';
        } else {
            encodedMsg += '%';
            encodedMsg += hex[(unsigned char)c >> 4];
            encodedMsg += hex[c & 0xf];
        }
    }
    return encodedMsg;
}

namespace ATAPI {
    bool MediaTypeCodeIsAudioCD(MediaTypeCode code) {
        return code == MTC_AUDIO_120MM || code == MTC_AUDIO_80MM || code == MTC_AUDIO_DATA_120MM || code == MTC_AUDIO_DATA_80MM ||
            code == MTC_CDR_AUDIO_120MM || code == MTC_CDR_AUDIO_80MM || code == MTC_CDR_AUDIO_DATA_120MM || code == MTC_CDR_AUDIO_DATA_80MM ||
            code == MTC_CD_EXT_AUDIO_120MM || code == MTC_CD_EXT_AUDIO_80MM || code == MTC_CD_EXT_AUDIO_DATA_120MM || code == MTC_CD_EXT_AUDIO_DATA_80MM
#ifndef ESPERCDP_HYBRID_CD_IS_NOT_AUDIO
            // Apparently some super old drives (the same C68E as always) report CD-Enhanced with a data session as hybrid CD (photo CD)
            // example: EXIT TUNES QWCE-20004 is reported as MTC_PHOTO_120MM
            || code == MTC_PHOTO_120MM || code == MTC_PHOTO_80MM || code == MTC_CDR_PHOTO_120MM || code == MTC_CDR_PHOTO_80MM || code == MTC_CD_EXT_PHOTO_120MM || code == MTC_CD_EXT_PHOTO_80MM
#endif
          ;
    }
};