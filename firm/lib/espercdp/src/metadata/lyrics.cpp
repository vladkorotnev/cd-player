#include <esper-cdp/lyrics.h>
#include <esper-cdp/utils.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <sstream>

namespace CD {
    void LyricProvider::process_lrc_line(const std::string& line, std::vector<Lyric>& lyrics) {
        ESP_LOGD(LOG_TAG,"Line = %s", line.c_str());

        std::string content = "";
        std::vector<int> times = {};

        int time = 0;
        content.reserve(line.length());

        bool token = false;
        unsigned int min = 0;
        unsigned int sec = 0;
        unsigned int decas = 0;
        int offset_millis = 0;

        bool skip_token = false;
        bool first_token = false;
        unsigned int* timepos = &min;

        if(line.substr(0, 9) == "[offset:") {
            offset_millis = -1 * std::stoi(line.substr(9, line.length() - 11));
            ESP_LOGI(LOG_TAG, "Offset = %i", offset_millis);
        }
        else {
            for(const char c: line) {
                if(!token) {
                    if(c == '[') {
                        token = true;
                        skip_token = false;
                        min = 0;
                        sec = 0;
                        decas = 0;
                    }
                    else if(!first_token) {
                        ESP_LOGV(LOG_TAG, "LRC line must start from token: %s", line.c_str());
                        break;
                    }
                    else {
                        if(!content.empty() || c != ' ')
                            content += c;
                    }
                }
                else if(skip_token) {
                    // ignore all until end of token
                    if(c == ']') {
                        token = false;
                    }
                }
                else {
                    if(c == ']') {
                        token = false;
                        int total_millis = (decas + 100 * (sec + 60 * min)) * 10;
                        times.push_back(total_millis);
                        ESP_LOGD(LOG_TAG, "Time: %02im%02is.%02id = %iF", min, sec, decas, total_frames);
                        first_token = true;
                    }
                    else if(c >= '0' && c <= '9') {
                        // number
                        *timepos *= 10;
                        *timepos += (c - '0');
                    }
                    else if(c == ':') {
                        // mm:ss separator
                        timepos = &sec;
                    }
                    else if(c == '.') {
                        // ms separator
                        timepos = &decas;
                    }
                    else if(c == ' ') {
                        // ignore
                    }
                    else {
                        skip_token = true;
                    }
                }
            }
        }

        if(!times.empty() && !content.empty()) {
            for(unsigned int ftime: times) {
                lyrics.push_back(Lyric {
                    .millisecond = ftime,
                    .line = content
                });
            }
        }
    }

    void LyricProvider::process_lrc_bulk(const std::string& bulk, std::vector<Lyric>& container) {
        std::string line;
        std::istringstream stream(bulk);
        while (std::getline(stream, line)) {
            process_lrc_line(line, container);
        }
        sort_lines(container);

        for(auto &l: container) {
            ESP_LOGV(LOG_TAG, "RSLT: (%i ms) %s", l.millisecond, l.line.c_str());
        }
    }

    void LyricProvider::sort_lines(std::vector<Lyric>& container) {
        std::sort(container.begin(), container.end(), [](const Lyric& l, const Lyric& r) {
            return l.millisecond < r.millisecond;
        });
    }

    void LrcLibLyricProvider::fetch_track(Track& track, const Album& album) {
        if(!track.lyrics.empty()) return;
        EXT_RAM_ATTR static WiFiClientSecure client;
        EXT_RAM_ATTR static HTTPClient http;

        const std::string& artist = (track.artist.empty() ? album.artist : track.artist);
        char url[512];
        snprintf(url, 512, "https://lrclib.net/api/get?artist_name=%s&track_name=%s", urlEncode(artist).c_str(), urlEncode(track.title).c_str()); // TODO duration

        client.setInsecure();
        client.setTimeout(5000);

        http.begin(client, url);

        client.setInsecure();
        client.setTimeout(5000);

        ESP_LOGI(LOG_TAG, "Query: %s", url);
        int response = http.GET();
        if(response == HTTP_CODE_OK) {
            EXT_RAM_ATTR static JsonDocument response;
            DeserializationError error = deserializeJson(response, http.getStream());

            if (error) {
                ESP_LOGE(LOG_TAG, "Parse error: %s", error.c_str());
            } else {
                if(response["syncedLyrics"].is<JsonString>()) {
                    process_lrc_bulk(response["syncedLyrics"].as<std::string>(), track.lyrics);
                } else {
                    ESP_LOGI(LOG_TAG, "no synced lyrics");
                }
            }
        } else {
            ESP_LOGW(LOG_TAG, "HTTP error %i", response);
        }

        http.end();
    }
}