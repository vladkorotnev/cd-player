#include <esper-cdp/metadata.h>
#include <esper-core/miniz_ext.h>
#include <iostream>
#include <LittleFS.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <utime.h>

static const char LOG_TAG[] = "CDCache";

// Cache structure:
// .../folder/A/Asdfgb.CAC
// File structure:
// - Header
// - compressed_size bytes array of {
//     CacheDataFileEntryHeader
//     null-terminated UTF8 string, if empty then just 0x00
// }

#define CACHE_DATAFILE_EXT ".CAC"
#define CACHE_DATAFILE_MAGIC 0x43414321 // '!CAC'
#define CACHE_DATAFILE_VER 0x0001

struct __attribute__((packed)) CacheDataFileHeader {
    uint32_t magic;
    uint16_t version;
    uint8_t entry_count; // normally (track count of disc + 1)*2 for the whole album
    uint32_t raw_size;
    uint32_t compressed_size;
};

enum CacheDataFileEntryKind: uint8_t {
    CACHE_ENTRY_TITLE = 0,
    CACHE_ENTRY_ARTIST
};

struct __attribute__((packed)) CacheDataFileEntryHeader {
    CacheDataFileEntryKind kind;
    uint8_t track_no; // 0 for whole CD, 1-99 for track
};

struct __attribute__((packed)) CacheDataFileEntry {
    CacheDataFileEntryHeader hdr;
    char data[];
};

void _entry_to_vec(std::vector<uint8_t>& v, const std::string& artist, const std::string& title, uint8_t trk_no, uint8_t* entry_counter) {
    CacheDataFileEntryHeader hdr;
    uint8_t * tmp = (uint8_t*) &hdr;
    if(artist != "") {
        hdr.kind = CACHE_ENTRY_ARTIST;
        hdr.track_no = trk_no;
        v.insert(v.end(), tmp, tmp + sizeof(hdr));
        std::copy(artist.begin(), artist.end(), std::back_inserter(v));
        v.push_back(0);
        *entry_counter += 1;
    }

    if(title != "") {
        hdr.kind = CACHE_ENTRY_TITLE;
        hdr.track_no = trk_no;
        v.insert(v.end(), tmp, tmp + sizeof(hdr));
        std::copy(title.begin(), title.end(), std::back_inserter(v));
        v.push_back(0);
        *entry_counter += 1;
    }
}

namespace CD {
    CachingMetadataAggregateProvider::CachingMetadataAggregateProvider(const char * cache_path) {
        path = (cache_path == nullptr ? "" : std::string(cache_path));
        if(!path.empty()) mk_dir_if_needed(path.c_str());
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
        auto path = id_to_path(id);

        FILE *f = nullptr;
        void * comp_data = nullptr;
        size_t total = 0;
        size_t r = 0;
        void * decomp_data = nullptr;
        void * decomp_data_ptr = nullptr;

        f = fopen(path.c_str(), "rb");
        if(!f) return false;

        CacheDataFileHeader hdr;
        if(sizeof(hdr) != fread(&hdr, 1, sizeof(hdr), f)) {
            ESP_LOGE(LOG_TAG, "Error reading header from %s: read less bytes than expected", path.c_str());
            goto kill_file;
        }

        if(hdr.magic != CACHE_DATAFILE_MAGIC) {
            ESP_LOGE(LOG_TAG, "%s: bad header magic (got 0x%08x, expected 0x%08x)", path.c_str(), hdr.magic, CACHE_DATAFILE_MAGIC);
            goto kill_file;
        }

        if(hdr.version != CACHE_DATAFILE_VER) {
            ESP_LOGE(LOG_TAG, "%s: bad header version (got 0x%04x, expected 0x%04x)", path.c_str(), hdr.version, CACHE_DATAFILE_VER);
            goto kill_file;
        }

        comp_data = heap_caps_malloc_prefer(hdr.compressed_size, 2, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM, MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT);
        if(comp_data == nullptr) {
            ESP_LOGE(LOG_TAG, "%s: failed allocating %lu bytes", path.c_str(), hdr.compressed_size);
            goto kill_file;
        }

        total = 0; do {
            r = fread(&((uint8_t*) comp_data)[total], 1, hdr.compressed_size - total, f);
            if(r > 0) total += r;
        } while(total < hdr.compressed_size && r > 0);

        if(total != hdr.compressed_size) {
            ESP_LOGE(LOG_TAG, "%s: failed reading %lu compressed bytes, only got %lu", path.c_str(), hdr.compressed_size, total);
            goto kill_file;
        }

        decomp_data = decompress_emplace(comp_data, total, hdr.raw_size);
        decomp_data_ptr = decomp_data;
        comp_data = nullptr;

        if(decomp_data == nullptr) {
            ESP_LOGE(LOG_TAG, "%s: failed decompressing data", path.c_str());
            goto kill_file;
        }
        
        for(int i = 0; i < hdr.entry_count; i++) {
            CacheDataFileEntry* cur_entry = (CacheDataFileEntry*) decomp_data_ptr;
            CacheDataFileEntryHeader* cur_data = &cur_entry->hdr;
            const char * val = cur_entry->data;
            switch(cur_data->kind) {
                case CACHE_ENTRY_ARTIST:
                    if(cur_data->track_no == 0) album.artist = std::string(val);
                    else if(cur_data->track_no - 1 < album.tracks.size()) album.tracks[cur_data->track_no - 1].artist = std::string(val);
                    else {
                        ESP_LOGE(LOG_TAG, "%s: track # too high: %i", path.c_str(), cur_data->track_no);
                        goto kill_file;
                    }
                    break;
                
                case CACHE_ENTRY_TITLE:
                    if(cur_data->track_no == 0) album.title = std::string(val);
                    else if(cur_data->track_no - 1 < album.tracks.size()) album.tracks[cur_data->track_no - 1].title = std::string(val);
                    else {
                        ESP_LOGE(LOG_TAG, "%s: track # too high: %i", path.c_str(), cur_data->track_no);
                        goto kill_file;
                    }
                    break;
                
                default:
                    ESP_LOGE(LOG_TAG, "%s: malformed or unknown entry type %i", path.c_str(), cur_data->kind);
                    goto kill_file;
                    return false;
            }

            decomp_data_ptr += sizeof(CacheDataFileEntry) + strlen(val) + 1 /*nul terminator*/;
        }

        free(decomp_data);
        fclose(f);

        return true;

    kill_file:
        if(comp_data != nullptr) free(comp_data);
        if(decomp_data != nullptr) free(decomp_data);
        fclose(f);
        ESP_LOGW(LOG_TAG, "File %s is broken and must be removed", path.c_str());
        remove(path.c_str());
        return false;
    }

    void CachingMetadataAggregateProvider::save_to_cache(const Album& album, const std::string id) {
        CacheDataFileHeader hdr = { 0 };
        hdr.magic = CACHE_DATAFILE_MAGIC;
        hdr.version = CACHE_DATAFILE_VER;

        std::vector<uint8_t> v = {};

        _entry_to_vec(v, album.artist, album.title, 0, &hdr.entry_count);
        for(auto& t: album.tracks) {
            _entry_to_vec(v, t.artist, t.title, t.disc_position.number, &hdr.entry_count);
        }

        hdr.raw_size = v.size();
        ESP_LOGI(LOG_TAG, "Size before compression = %lu", hdr.raw_size);

        void * dest = malloc(hdr.raw_size); // gotta fit?
        if(dest == nullptr) {
            ESP_LOGE(LOG_TAG, "OOM allocating %i bytes!", hdr.raw_size);
            return;
        }

        mz_stream stream;
        memset(&stream, 0, sizeof(stream));
        stream.next_in = v.data();
        stream.avail_in = hdr.raw_size;
        stream.next_out = (unsigned char*)dest;
        stream.avail_out = hdr.raw_size;

        int rslt = mz_deflateInit2(&stream, MZ_BEST_COMPRESSION, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY);
        if(rslt != MZ_OK) {
            ESP_LOGE(LOG_TAG, "DeflateInit error %i", rslt);
            free(dest);
            return;
        }

        rslt = mz_deflate(&stream, MZ_FINISH);
        if(rslt != MZ_STREAM_END) {
            ESP_LOGE(LOG_TAG, "Deflate error %i", rslt);
            mz_deflateEnd(&stream);
            free(dest);
            return;
        }

        mz_deflateEnd(&stream);
        hdr.compressed_size = stream.total_out;
        ESP_LOGI(LOG_TAG, "Size after compression = %lu", hdr.compressed_size);

        // TODO: check if the file will fit, if not then delete the oldest files in cache folder

        mk_dir_if_needed(id_to_dir_prefix(id).c_str());
        auto path = id_to_path(id);
        FILE * f;
        f = fopen(path.c_str(), "wb");
        if(!f) {
            ESP_LOGE(LOG_TAG, "Failed to create %s", path.c_str());
            free(dest);
            return;
        }

        size_t written = fwrite(&hdr, 1, sizeof(hdr), f);
        if(written != sizeof(hdr)) {
            ESP_LOGE(LOG_TAG, "Failed to write header to %s: expected %i bytes, wrote %i", path.c_str(), sizeof(hdr), written);
            free(dest);
            fclose(f);
            remove(path.c_str());
            return;
        }

        written = fwrite(dest, 1, hdr.compressed_size, f);
        if(written != hdr.compressed_size) {
            ESP_LOGE(LOG_TAG, "Failed to write content to %s: expected %i bytes, wrote %i", path.c_str(), hdr.compressed_size, written);
            free(dest);
            fclose(f);
            remove(path.c_str());
            return;
        }

        fclose(f);
        free(dest);

        ESP_LOGI(LOG_TAG, "Remaining FS capacity after cache = %i", LittleFS.totalBytes() - LittleFS.usedBytes());
    }

    const std::string CachingMetadataAggregateProvider::id_to_dir_prefix(const std::string& id) {
        return path + "/" + id.substr(0, 1);
    }

    const std::string CachingMetadataAggregateProvider::id_to_path(const std::string& id) {
        return id_to_dir_prefix(id) + "/" + id + CACHE_DATAFILE_EXT;
    }

    void CachingMetadataAggregateProvider::mk_dir_if_needed(const char * path) {
        struct stat st = {0};
        if (stat(path, &st) == -1) {
            mkdir(path, 0777);
        }
    }
};