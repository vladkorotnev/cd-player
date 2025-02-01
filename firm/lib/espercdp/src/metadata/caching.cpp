#include <esper-cdp/metadata.h>

namespace CD {
    void CachingMetadataAggregateProvider::fetch_album(Album& album) {
        // TODO: check cache
        // If no cache, query upstream and cache, deleting older albums as needed

        for(auto &provider: providers) {
            provider->fetch_album(album);
            if(album.is_metadata_complete()) break;
        }

        // TODO: update cache
    }
};