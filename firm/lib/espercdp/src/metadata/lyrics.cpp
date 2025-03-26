#include <esper-cdp/lyrics.h>
#include <esper-cdp/utils.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <sstream>
#include <mbedtls/base64.h>

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
        unsigned int centis = 0;
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
                        centis = 0;
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
                        int total_millis;
                        if(centis < 100) {
                            total_millis = (centis + 100 * (sec + 60 * min)) * 10;
                        } else {
                            // some writers seem to output in ms rather than centiseconds (first encountered in NetEase's lyrics for MELL - Red Fraction)
                            total_millis = (centis + 1000 * (sec + 60 * min));
                        }

                        times.push_back(total_millis);
                        ESP_LOGD(LOG_TAG, "Time: %02im%02is.%02id = %i ms", min, sec, centis, total_millis);
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
                        timepos = &centis;
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
        std::string last_line;
        std::istringstream stream(bulk);
        while (std::getline(stream, line)) {
            process_lrc_line(line, container);
            last_line = line;
        }
        if(last_line != line) {
            // Sometimes getline doesn't return one line, IDK why -- IIRC the spec says it must
            // It does get it though it seems, so let's parse it
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

    void QQMusicLyricProvider::fetch_track(Track& track, const Album& album) {
        // Thanks to: https://github.com/jacquesh/foo_openlyrics/blob/main/src/sources/qqmusic.cpp
        if(!track.lyrics.empty()) return;
        EXT_RAM_ATTR static WiFiClient client;
        EXT_RAM_ATTR static HTTPClient http;
        const char * referer = "http://y.qq.com/portal/player.html";
        const std::string& artist = (track.artist.empty() ? album.artist : track.artist);

        char url[512];
        snprintf(url, 512, "http://c.y.qq.com/splcloud/fcgi-bin/smartbox_new.fcg?inCharset=utf-8&outCharset=utf-8&key=%s+%s", urlEncode(artist).c_str(), urlEncode(track.title).c_str());

        client.setTimeout(5000);
        http.addHeader("Referer", referer);
        http.begin(client, url);

        ESP_LOGI(LOG_TAG, "Query: %s", url);
        int response = http.GET();
        if(response == HTTP_CODE_OK) {
            EXT_RAM_ATTR static JsonDocument json;
            DeserializationError error = deserializeJson(json, http.getStream());

            if (error) {
                ESP_LOGE(LOG_TAG, "Parse error: %s", error.c_str());
            } else {
                if(json["data"]["song"].is<JsonObject>()) {
                    if(json["data"]["song"]["itemlist"].is<JsonArray>()) {
                        const JsonArray arr = json["data"]["song"]["itemlist"].as<JsonArray>();
                        if(arr.size() == 1) {
                            if(arr[0]["mid"].is<JsonString>()) {
                                const JsonString mid = arr[0]["mid"].as<JsonString>();
                                snprintf(url, 512, "http://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg?g_tk=5381&format=json&inCharset=utf-8&outCharset=utf-8&songmid=%s", mid.c_str());
                                http.end();
                                http.addHeader("Referer", referer);
                                http.begin(client, url);
                                response = http.GET();
                                if(response == HTTP_CODE_OK) {
                                    DeserializationError error = deserializeJson(json, http.getStream());
                                    if (error) {
                                        ESP_LOGE(LOG_TAG, "Parse error in fcg_query_lyric_new: %s", error.c_str());
                                    } else {
                                        if(json["lyric"].is<JsonString>()) {
                                            const JsonString lyric_b64 = json["lyric"].as<JsonString>();
                                            size_t actual_size = 0;
                                            int rslt = mbedtls_base64_decode(nullptr, 0, &actual_size, (const unsigned char*) lyric_b64.c_str(), lyric_b64.size());
                                            if(rslt == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
                                                char * lyric_cstr = (char*) ps_malloc(actual_size + 1);
                                                lyric_cstr[actual_size] = 0;
                                                rslt = mbedtls_base64_decode((unsigned char*) lyric_cstr, actual_size, &actual_size, (const unsigned char*) lyric_b64.c_str(), lyric_b64.size());
                                                if(rslt == 0) {
                                                    std::string str(lyric_cstr); // this is copying... how to prevent? someday
                                                    free(lyric_cstr);
                                                    process_lrc_bulk(str, track.lyrics); 
                                                    ESP_LOGI(LOG_TAG, "Got %i lines of lyrics", track.lyrics.size());
                                                } else {
                                                    ESP_LOGE(LOG_TAG, "Could not decode lyrics: mbedtls_base64_decode error %i", rslt);
                                                }
                                            } else {
                                                ESP_LOGE(LOG_TAG, "Could not get length: mbedtls_base64_decode returned %i", rslt);
                                            }
                                        } else {
                                            ESP_LOGW(LOG_TAG, "No lyric entry in fcg_query_lyric_new");
                                        }
                                    }
                                } else {
                                    ESP_LOGE(LOG_TAG, "HTTP code %i in fcg_query_lyric_new", response);
                                }
                            } else {
                                ESP_LOGW(LOG_TAG, "no mid field in song");
                            }
                        } else {
                            ESP_LOGW(LOG_TAG, "itemlist.size is %i", arr.size());
                        }
                    } else {
                        ESP_LOGW(LOG_TAG, "no itemlist in response");
                    }
                } else {
                    ESP_LOGI(LOG_TAG, "no song object in response");
                }
            }
        } else {
            ESP_LOGW(LOG_TAG, "HTTP error %i", response);
        }

        http.end();
    }

    void NeteaseLyricProvider::fetch_track(Track& track, const Album& album) {
        // Ref: https://github.com/jacquesh/foo_openlyrics/blob/45546bdb5d567b04ed10e99b723147e128efbd8a/src/sources/netease.cpp
        if (!track.lyrics.empty()) return;

        EXT_RAM_ATTR static WiFiClient client;
        EXT_RAM_ATTR static HTTPClient http;

        const std::string& artist = (track.artist.empty() ? album.artist : track.artist);
        std::string query_url = "http://music.163.com/api/search/get?s=" + urlEncode(artist + " " + track.title) + "&type=1&offset=0&sub=false&limit=5";
        
        client.setTimeout(5000);

        http.begin(client, query_url.c_str());
        http.addHeader("Referer", "http://music.163.com/");
        http.addHeader("Cookie", "appver=2.0.2");
        http.addHeader("Charset", "utf-8");
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        http.addHeader("X-Real-IP", "202.96.0.0");

        ESP_LOGI(LOG_TAG, "Query: %s", query_url.c_str());
        int response = http.GET();
        if (response == HTTP_CODE_OK) {
            EXT_RAM_ATTR static JsonDocument json;
            DeserializationError error = deserializeJson(json, http.getStream());

            if (error) {
                ESP_LOGE(LOG_TAG, "Parse error in search: %s", error.c_str());
            } else {
                if(json["result"].is<JsonObject>()) {
                    if (json["result"]["songs"].is<JsonArray>()){
                        const JsonArray song_arr = json["result"]["songs"].as<JsonArray>();

                        if (song_arr.size() > 0 && song_arr[0].is<JsonObject>()) {
                            const JsonObject first_song = song_arr[0].as<JsonObject>();

                            if(first_song["id"].is<long>()) {
                                const long song_id = first_song["id"].as<long>();
                                http.end();
                                char lyric_url[256];
                                snprintf(lyric_url, sizeof(lyric_url), "http://music.163.com/api/song/lyric?tv=-1&kv=-1&lv=-1&os=pc&id=%li", song_id);
                                http.begin(client, lyric_url);
                                http.addHeader("Referer", "http://music.163.com/");
                                http.addHeader("Cookie", "appver=2.0.2");
                                http.addHeader("Charset", "utf-8");
                                http.addHeader("Content-Type", "application/x-www-form-urlencoded");
                                http.addHeader("X-Real-IP", "202.96.0.0");

                                ESP_LOGV(LOG_TAG, "Lyric url: %s", lyric_url);
                                response = http.GET();

                                if (response == HTTP_CODE_OK){
                                    DeserializationError error = deserializeJson(json, http.getStream());
                                    if (error){
                                        ESP_LOGE(LOG_TAG, "Parse error in lyric: %s", error.c_str());
                                    } else {
                                        if (json["lrc"].is<JsonObject>() && json["lrc"]["lyric"].is<JsonString>()){
                                            const JsonString lyric_str = json["lrc"]["lyric"].as<JsonString>();
                                            ESP_LOGD(LOG_TAG, "LYRIC: %s", lyric_str.c_str());
                                            process_lrc_bulk(lyric_str.c_str(), track.lyrics);
                                            ESP_LOGI(LOG_TAG, "Got %i lines of lyrics", track.lyrics.size());
                                        } else {
                                            ESP_LOGW(LOG_TAG, "No lrc or lyric entry in response");
                                        }
                                    }
                                } else {
                                    ESP_LOGE(LOG_TAG, "HTTP error %i in lyric", response);
                                }
                            }
                        } else {
                            ESP_LOGW(LOG_TAG, "No songs found in result");
                        }
                    } else {
                      ESP_LOGW(LOG_TAG,"No song array");
                    }
                } else {
                    ESP_LOGW(LOG_TAG,"No result object");
                }
            }
        } else {
            ESP_LOGE(LOG_TAG, "HTTP error %i in search", response);
        }
        http.end();
    }
}