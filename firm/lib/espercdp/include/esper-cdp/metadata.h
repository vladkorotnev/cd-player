#pragma once
#include <esper-cdp/types.h>
#include <esper-cdp/atapi.h>
#include <string>
#include <vector>
#include <fstream>

namespace CD {
    struct Track {
        ATAPI::DiscTrack disc_position;
        std::string title;
        std::string artist;
    };

    class Album {
    public:
        Album(): 
            tracks({}),
            duration( {.M = 0, .S = 0, .F = 0} ),
            lead_out( {.M = 0, .S = 0, .F = 0} ),
            title(""),
            artist(""),
            toc_subchannel({})
        {}

        Album(const ATAPI::DiscTOC& toc): Album() {
            lead_out = toc.leadOut;
            if(toc.tracks.back().is_data) {
                // Normally data track is at the end
                duration = toc.tracks.back().position;
            } else {
                duration = lead_out;
            }
            toc_subchannel = toc.toc_subchannel;
            char buf[16] = { 0 };

            for(auto& track: toc.tracks) {
                tracks.push_back({
                    .disc_position = track,
                    .title = "",
                    .artist = ""
                });
            }
        }

        std::vector<uint8_t> toc_subchannel;
        std::string title;
        std::string artist;
        std::vector<Track> tracks;
        MSF duration;
        MSF lead_out;

        bool is_metadata_complete() {
            for(auto& track: tracks) {
                if(track.artist.length() == 0 || track.title.length() == 0) {
                    return false;
                }
            }

            return (title.length() != 0 && artist.length() != 0);
        }

        bool is_metadata_good_for_caching() {
            for(auto& track: tracks) {
                if(track.title.length() == 0) {
                    return false;
                }
            }

            return (title.length() != 0);
        }
    };

    class MetadataProvider {
    public:
        virtual void fetch_album(Album&) {}
    };

    class CachingMetadataAggregateProvider: public MetadataProvider {
    public:
        CachingMetadataAggregateProvider(const char * cache_path);
        void fetch_album(Album&) override;
        std::vector<MetadataProvider *> providers = {};
    private:
        std::string path;

        bool populate_from_cache(Album&, const std::string);
        void save_to_cache(const Album&, const std::string);
    };

    class MusicBrainzMetadataProvider: public MetadataProvider {
    public:
        MusicBrainzMetadataProvider() {}
        void fetch_album(Album&) override;
        static const std::string generate_id(const Album&); // <- we will use it for cache key, thus exposing it
    };

    class CDDBMetadataProvider: public MetadataProvider {
    public:
        CDDBMetadataProvider(std::string serverUrl, std::string authEmail):
            server(serverUrl),
            email(authEmail)
        {}

        void fetch_album(Album&) override;

        std::string server;
        std::string email;
    };

    class CDTextMetadataProvider: public MetadataProvider {
    public:
        CDTextMetadataProvider() {}

        void fetch_album(Album&) override;
    };
};