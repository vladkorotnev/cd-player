#include <esper-cdp/metadata.h>
#include "esp_crc.h"

static const char LOG_TAG[] = "CDTXT";

namespace CD {
    struct CDTextPack {
        enum Kind: uint8_t {
            TITLE = 0x80,
            ARTIST = 0x81,
            // YAGNI the rest
        };
        
        Kind kind;
        uint8_t track_no;
        uint8_t sequence_no;

        uint8_t char_pos: 4;
        uint8_t block_no: 3;
        bool wide_char: 1;

        char payload[12];

        uint16_t crc;
    };

    void CDTextMetadataProvider::fetch_album(Album& album) {
        if(album.tracks.size() == 0) return; // probably not a CDA!
        
        auto raw_data = album.toc_subchannel;

        if(raw_data.size() == 0) {
            ESP_LOGW(LOG_TAG, "No CD text data");
            return;
        }

        CDTextPack * cur;
        std::vector<std::string> tmp_artists(album.tracks.size() + 1);
        std::vector<std::string> tmp_titles(album.tracks.size() + 1);

        uint8_t last_seq_no = 0xFF;
        int cur_trk_no_artist = 0;
        int cur_trk_no_title = 0;

        for(int pos = 0; pos < raw_data.size(); pos += sizeof(CDTextPack)) {
            cur = (CDTextPack*) &raw_data[pos];

            last_seq_no++;
            if(cur->sequence_no != last_seq_no) {
                ESP_LOGE(LOG_TAG, "Seq no jump from %i to %i at pos=%i, bail out!", last_seq_no, cur->sequence_no, pos);
                break;
            }

            if(cur->block_no != 0) continue; // maybe one day
            if(cur->wide_char) continue; // maybe one day

            ESP_LOGI(LOG_TAG, "Packet Kind=[%i] Track=[%i] Payload=[%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x] CRC=[%08x]", cur->kind, cur->track_no, 
                cur->payload[0], cur->payload[1], cur->payload[2], cur->payload[3],cur->payload[4],cur->payload[5],cur->payload[6],cur->payload[7],cur->payload[8],cur->payload[9],cur->payload[10],cur->payload[11],
                cur->crc);

            if(cur->kind == CDTextPack::Kind::TITLE) {
                cur_trk_no_title = cur->track_no;
                for(int i = 0; i < sizeof(cur->payload); i++) {
                    const char p = cur->payload[i];

                    if (cur_trk_no_title >= tmp_titles.size()) {
                        ESP_LOGE(LOG_TAG, "Seems to have malformed CD TEXT data. Tried to write title for track %i. Skipping the rest of this pack...", cur_trk_no_title);
                        break;
                    }

                    if(p == 0) {
                        cur_trk_no_title++;
                    } else if(p == 0x9 && cur_trk_no_title > 0) {
                        tmp_titles[cur_trk_no_title] = tmp_titles[cur_trk_no_title - 1];
                    } else {
                        tmp_titles[cur_trk_no_title] += p;
                    }
                }
            }
            else if(cur->kind == CDTextPack::Kind::ARTIST) {
                cur_trk_no_artist = cur->track_no;
                for(int i = 0; i < sizeof(cur->payload); i++) {
                    const char p = cur->payload[i];

                    if (cur_trk_no_artist >= tmp_artists.size()) {
                        ESP_LOGE(LOG_TAG, "Seems to have malformed CD TEXT data. Tried to write artist for track %i. Skipping the rest of this pack...", cur_trk_no_artist);
                        break;
                    }

                    if(p == 0) {
                        cur_trk_no_artist++;
                    } else if(p == 0x9 && cur_trk_no_artist > 0) {
                        tmp_artists[cur_trk_no_artist] = tmp_artists[cur_trk_no_artist - 1];
                    } else {
                        tmp_artists[cur_trk_no_artist] += p;
                    }
                }
            }
        }

        if(album.artist.empty()) album.artist = tmp_artists[0];
        if(album.title.empty()) album.title = tmp_titles[0];
        for(int i = 0; i < tmp_artists.size(); i++) {
            ESP_LOGI(LOG_TAG, "Artist %i: %s", i, tmp_artists[i].c_str());

            if(i > 0 && album.tracks[i - 1].artist.empty() && tmp_artists[0] != tmp_artists[i]) {
                album.tracks[i - 1].artist = tmp_artists[i];
            }
        }
        for(int i = 0; i < tmp_titles.size(); i++) {
            ESP_LOGI(LOG_TAG, "Title %i: %s", i, tmp_titles[i].c_str());

            if(i > 0 && album.tracks[i - 1].title.empty()) {
                album.tracks[i - 1].title = tmp_titles[i];
            }
        }
    }
}