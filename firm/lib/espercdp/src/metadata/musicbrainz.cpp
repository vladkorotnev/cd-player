#include <esper-cdp/metadata.h>
#include <esper-cdp/utils.h>
#include <mbedtls/sha1.h>
#include <cstring>
#include <esp32-hal-log.h>

static const char LOG_TAG[] = "MBRAINZ";

namespace CD {
    void MusicBrainzMetadataProvider::fetch_album(Album& album) {
        auto disc_id = generate_id(album);
        ESP_LOGI(LOG_TAG, "Disc ID = %s", disc_id.c_str());
        
        // TODO: update album metadata from server
        ESP_LOGE(LOG_TAG, "TODO: Not implemented!");
    }

    const std::string MusicBrainzMetadataProvider::generate_id(const Album& album) {
        // Ref. https://musicbrainz.org/doc/Disc_ID_Calculation

        mbedtls_sha1_context ctx;
        mbedtls_sha1_init(&ctx);
        mbedtls_sha1_starts_ret(&ctx);

        char temp[16] = { 0 };
        unsigned char sha[20] = { 0 };
        size_t dummy;

        sprintf(temp, "%02X", album.tracks[0].disc_position.number);
        mbedtls_sha1_update_ret(&ctx, (unsigned char*) temp, strlen(temp));

        sprintf(temp, "%02X", album.tracks.back().disc_position.number);
        mbedtls_sha1_update_ret(&ctx, (unsigned char*) temp, strlen(temp));

        sprintf(temp, "%08X", MSF_TO_FRAMES(album.duration));
        mbedtls_sha1_update_ret(&ctx, (unsigned char*) temp, strlen(temp));

        for (int i = 0; i < 99; i++) {
            if(i < album.tracks.size()) {
                sprintf(temp, "%08X", MSF_TO_FRAMES(album.tracks[i].disc_position.position));
                mbedtls_sha1_update_ret(&ctx, (unsigned char*) temp, strlen(temp));
            }
            else {
                mbedtls_sha1_update_ret(&ctx, (unsigned char*) "00000000", 8);
            }
        }

        mbedtls_sha1_finish_ret(&ctx, sha);
        mbedtls_sha1_free(&ctx);

        char * mbid = rfc822_binary(sha, 20, &dummy);
        ESP_LOGV(LOG_TAG, "Disc ID = %s", mbid);

        // use it like https://musicbrainz.org/ws/2/discid/F9XV7VTnpufEOtGcaHcitqk0k2s-?inc=recordings&fmt=json
        const std::string rslt = std::string(mbid);
        free(mbid);
        return rslt;
    }
};