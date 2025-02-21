#include <esper-cdp/metadata.h>

namespace CD {
    class LyricProvider: public MetadataProvider {
    public:
        bool cacheable() override { return false; }
        void fetch_album(Album& album) override {
            for(auto &track: album.tracks) {
                if(track.lyrics.empty() && !track.title.empty()) {
                    fetch_track(track, album);
                }
            }
        }

        virtual void fetch_track(Track& track, const Album& album);
    protected:
        void process_lrc_line(const std::string& line, std::vector<Lyric>& container);
        void process_lrc_bulk(const std::string& bulk, std::vector<Lyric>& container);
    private:
        void sort_lines(std::vector<Lyric>& container);
        const char * LOG_TAG = "LYRCom";
    };

    class LrcLibLyricProvider: public LyricProvider {
    // POC
    public:
        LrcLibLyricProvider() {}

        void fetch_track(Track& track, const Album& album) override;
    private:
        const char * LOG_TAG = "LRCLIB";
    };
}