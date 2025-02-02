#pragma once
#include <esper-cdp/types.h>
#include <esper-cdp/atapi.h>
#include <string>
#include <vector>
#include <memory>

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
            title(""),
            artist("")
        {}

        Album(const ATAPI::DiscTOC& toc): Album() {
            duration = toc.leadOut;
            char buf[16] = { 0 };

            for(auto& track: toc.tracks) {
                tracks.push_back({
                    .disc_position = track,
                    .title = "",
                    .artist = ""
                });
            }
        }

        std::string title;
        std::string artist;
        std::vector<Track> tracks;
        MSF duration;

        bool is_metadata_complete() {
            for(auto& track: tracks) {
                if(track.artist.length() == 0 || track.title.length() == 0) {
                    return false;
                }
            }

            return (title.length() != 0 && artist.length() != 0);
        }
    };

    class MetadataProvider {
    public:
        virtual void fetch_album(Album&) {}
    };

    class CachingMetadataAggregateProvider: public MetadataProvider {
    public:
        CachingMetadataAggregateProvider() {}
        void fetch_album(Album&) override;
        std::vector<MetadataProvider *> providers = {};
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
        // TODO: Device needs to be thread safe!! at least in the CDText part
        CDTextMetadataProvider(ATAPI::Device* drive): _dev(drive) {}

        void fetch_album(Album&) override;

    private:
        ATAPI::Device * _dev;
    };
};