#include <esper-cdp/metadata.h>
#include <iostream>
#include <LittleFS.h>

static const char LOG_TAG[] = "CDCache";

namespace CD {
    // Cache structure:
    // - folder
    // +---- ... (disc id).cac
    // 
    // TODO come up with a way of deleting old IDs... maybe just by number of items?
    // TODO compression probably wouldn't hurt either

    CachingMetadataAggregateProvider::CachingMetadataAggregateProvider(const char * cache_path) {
        path = (cache_path == nullptr ? "" : std::string(cache_path));
    }

    void CachingMetadataAggregateProvider::fetch_album(Album& album) {
        std::string id = MusicBrainzMetadataProvider::generate_id(album);

        if(populate_from_cache(album, id)) {
            ESP_LOGI(LOG_TAG, "Loaded from cache: %s", id.c_str());
        } else {
            for(auto &provider: providers) {
                if(!provider->cacheable()) continue;
                provider->fetch_album(album);
                if(album.is_metadata_complete()) break;
            }

            if(album.is_metadata_good_for_caching()) save_to_cache(album, id);
        }

        // in the end query non cacheable providers such as CDTEXT or lyrics
        for(auto &provider: providers) {
            if(provider->cacheable()) continue;

            provider->fetch_album(album);
        }
    }

    bool CachingMetadataAggregateProvider::populate_from_cache(Album& album, const std::string id) {
        const std::string fpath = path + '/' + id + ".cac";
        std::ifstream infile(fpath);
        std::string line;

        if(infile.is_open()) {
            if(!getline(infile, line)) return false;
            album.title = line;

            if(!getline(infile, line)) return false;
            album.artist = line;

            for(int i = 0; i < album.tracks.size(); i++) {
                if(!getline(infile, line)) return false;
                album.tracks[i].title = line;

                if(!getline(infile, line)) return false;
                album.tracks[i].artist = line;
            }

            return true;
        }

        return false;
    }

    void CachingMetadataAggregateProvider::save_to_cache(const Album& album, const std::string id) {
        const std::string fpath = path + '/' + id + ".cac";
        bool success = false;
        int attempts = 3;

        while(!success && (attempts--) > 0) {
            std::ofstream output(fpath, std::fstream::out);
            if(output.is_open()) {
                output << album.title << '\n';
                output << album.artist << '\n';
                for(int i = 0; i < album.tracks.size(); i++) {
                    output << album.tracks[i].title << '\n';
                    output << album.tracks[i].artist << '\n';
                }

                output.flush();
                if(output.fail()) {
                    ESP_LOGE(LOG_TAG, "Write failure: errno=%i (%s)", errno, strerror(errno));
                    if(errno == ENOSPC) {
                        // TODO: delete old ID's cache somehow
                    }
                } else {
                    output.close();
                    if(output.fail()) {
                        ESP_LOGE(LOG_TAG, "Close failure: errno=%i (%s)", errno, strerror(errno));
                    } else {
                        success = true;
                    }
                }
            } else {
                ESP_LOGE(LOG_TAG, "Cache file creation failed, bail out!");
                break;
            }
        }

        ESP_LOGI(LOG_TAG, "Remaining FS size = %i", LittleFS.totalBytes() - LittleFS.usedBytes());
    }
};