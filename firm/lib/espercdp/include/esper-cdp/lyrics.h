#include <esper-cdp/metadata.h>
#include <atomic>

namespace CD {
    class LyricProvider: public MetadataProvider {
    public:
        bool cacheable() override { return false; }
        void fetch_album(Album& album) override;
        void cancel() override;
        void reset_cancel() override;

        virtual void fetch_track(Track& track, const Album& album);
    protected:
        bool is_cancelled() const { return cancel_requested.load(); }
        void process_lrc_line(const std::string& line, std::vector<Lyric>& container);
        void process_lrc_bulk(const std::string& bulk, std::vector<Lyric>& container);
    private:
        std::atomic<bool> cancel_requested {false};
        void sort_lines(std::vector<Lyric>& container);
        const char * LOG_TAG = "LYRCom";
    };

    class LrcLibLyricProvider: public LyricProvider {
    public:
        LrcLibLyricProvider() {}

        void fetch_track(Track& track, const Album& album) override;
    private:
        const char * LOG_TAG = "LRCLIB";
    };

    class QQMusicLyricProvider: public LyricProvider {
    public:
        QQMusicLyricProvider() {}
        void fetch_track(Track& track, const Album& album) override;
    private:
        const char * LOG_TAG = "QQLRC";
    };

    class NeteaseLyricProvider: public LyricProvider {
    public:
        NeteaseLyricProvider() {}
        void fetch_track(Track& track, const Album& album) override;
    private:
        const char * LOG_TAG = "NetEase";
    };
}
