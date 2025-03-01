#pragma once
#include <esper-cdp/types.h>
#include <esper-cdp/atapi.h>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>

namespace CD {
    struct Lyric {
        uint32_t millisecond;
        std::string line;
    };

    struct Track {
        ATAPI::DiscTrack disc_position;
        std::string title;
        std::string artist;
        std::vector<Lyric> lyrics;
    };

    class Album {
    public:
        Album(): 
            tracks({}),
            duration( {.M = 0, .S = 0, .F = 0} ),
            lead_out( {.M = 0, .S = 0, .F = 0} ),
            title(""),
            artist(""),
            toc_subchannel({}),
            toc({})
        {}

        Album(const ATAPI::DiscTOC& _toc): Album() {
            lead_out = _toc.leadOut;
            if(_toc.tracks.back().is_data) {
                // Normally data track is at the end
                duration = _toc.tracks.back().position;
            } else {
                duration = lead_out;
            }
            toc = _toc.tracks;
            toc_subchannel = _toc.toc_subchannel;

            for(auto& track: _toc.tracks) {
                if(!track.is_data) {
                    tracks.push_back({
                        .disc_position = track,
                        .title = "",
                        .artist = ""
                    });
                }
            }
        }

        std::vector<ATAPI::DiscTrack> toc;
        std::vector<uint8_t> toc_subchannel;
        std::string title;
        std::string artist;
        std::vector<Track> tracks;
        MSF duration;
        MSF lead_out;

        bool is_metadata_complete() {
            for(auto& track: tracks) {
                if(track.title.empty()) {
                    return false;
                }
            }

            return (!title.empty() && !artist.empty());
        }

        bool is_metadata_good_for_caching() {
            for(auto& track: tracks) {
                if(track.title.empty()) {
                    return false;
                }
            }

            return !title.empty();
        }
    };

    class MetadataProvider {
    public:
        virtual ~MetadataProvider() = default;
        virtual void fetch_album(Album&) {}
        virtual bool cacheable() { return false; }
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
        void mk_dir_if_needed(const char * path);
        const std::string id_to_path(const std::string&);
        const std::string id_to_dir_prefix(const std::string&);
    };

    class MusicBrainzMetadataProvider: public MetadataProvider {
    public:
        MusicBrainzMetadataProvider() {}
        void fetch_album(Album&) override;
        bool cacheable() override { return true; }
        static const std::string generate_id(const Album&); // <- we will use it for cache key, thus exposing it
    };

    class CDDBMetadataProvider: public MetadataProvider {
    public:
        CDDBMetadataProvider(const std::string& serverUrl, const std::string& authEmail):
            server(serverUrl),
            email(authEmail)
        {}

        void fetch_album(Album&) override;
        bool cacheable() override { return true; }

        std::string server;
        std::string email;
    };

    class CDTextMetadataProvider: public MetadataProvider {
    public:
        CDTextMetadataProvider() {}

        void fetch_album(Album&) override;
    };
};