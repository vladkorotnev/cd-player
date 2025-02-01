#include <esper-cdp/metadata.h>
#include <esp32-hal-log.h>
#include <cddb/cddb.h>
#include <cddb/cddb_ni.h>

static const char LOG_TAG[] = "CDDB";

namespace CD {
    void CDDBMetadataProvider::fetch_album(Album& album) {
        int matches = 0;
        cddb_disc_t * disc = NULL;
        cddb_track_t * trk = NULL;

        auto cddb = cddb_new();
        if(!cddb) {
            ESP_LOGE(LOG_TAG, "memory allocation failed");
            return;
        }

        cddb_log_set_level(cddb_log_level_t::CDDB_LOG_INFO);

        cddb_cache_disable(cddb); // <- we have our own caching scheme, as e.g. GnuDB has incompatible IDs so caching by CD ID is pointless
        cddb_set_server_name(cddb, server.c_str());

        disc = cddb_disc_new();
        if(!disc) {
            ESP_LOGE(LOG_TAG, "memory allocation failed");
            goto bail;
        }

        cddb_disc_set_length(disc, FRAMES_TO_SECONDS(MSF_TO_FRAMES(album.duration)));

        for(int i = 0; i < album.tracks.size(); i++) {
            trk = cddb_track_new();
            if(!trk) {
                ESP_LOGE(LOG_TAG, "memory allocation failed");
                goto bail;
            }

            cddb_disc_add_track(disc, trk);
            cddb_track_set_frame_offset(trk, MSF_TO_FRAMES(album.tracks[i].disc_position.position));
        }

        cddb_disc_calc_discid(disc);
        ESP_LOGI(LOG_TAG, "Disc ID = %08x", cddb_disc_get_discid(disc));

        matches = cddb_query(cddb, disc);
        if(matches == -1) {
            ESP_LOGE(LOG_TAG, "Query failed: (%i) %s", cddb_errno(cddb), cddb_error_str(cddb_errno(cddb)));
        } 
        else if(matches > 1) {
            ESP_LOGE(LOG_TAG, "Multiple matches found. Ignoring as there is no way to choose (for now)");
        }
        else if(matches == 0) {
            ESP_LOGW(LOG_TAG, "No matches");
        }
        else {
            bool success = cddb_read(cddb, disc);
            if(!success) {
                ESP_LOGE(LOG_TAG, "Read failed: (%i) %s", cddb_errno(cddb), cddb_error_str(cddb_errno(cddb)));
            } else {
                album.title = std::string(cddb_disc_get_title(disc));
                album.artist = std::string(cddb_disc_get_artist(disc));
                trk = cddb_disc_get_track_first(disc);
                for(int i = 0; i < album.tracks.size(); i++) {
                    // Assuming album.tracks is in the track# order
                    if(trk != NULL) {
                        const char * tmp = nullptr;

                        tmp = cddb_track_get_title(trk);
                        if(tmp != nullptr) album.tracks[i].title = std::string(tmp);

                        tmp = cddb_track_get_artist(trk);
                        if(tmp != nullptr && !strcmp(tmp, album.artist.c_str())) album.tracks[i].artist = std::string(tmp);

                        trk = cddb_disc_get_track_next(disc);
                    }
                }
            }
        }

    bail:
        if(disc) cddb_disc_destroy(disc);
        if(cddb) cddb_destroy(cddb);
    }
}