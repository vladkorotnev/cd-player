#include <esper-cdp/metadata.h>
#include <esper-cdp/utils.h>
#include <mbedtls/sha1.h>
#include <cstring>
#include <esp32-hal-log.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static const char LOG_TAG[] = "MBRAINZ";

const std::string MBJsonToArtistName(const JsonObject& info) {
    std::string tmp_artist = "";

    if(info["artist-credit"].is<JsonArray>()) {
        auto credits = info["artist-credit"].as<JsonArray>();
        for(auto entry: credits) {
            if(entry["name"].is<JsonString>()) {
                tmp_artist.append(entry["name"].as<std::string>());
            }

            if(entry["joinphrase"].is<JsonString>()) {
                tmp_artist.append(entry["joinphrase"].as<std::string>());
            }
        }
    } else {
        ESP_LOGW(LOG_TAG, "There is not artist-credit");
    }

    return tmp_artist;
}

namespace CD {
    void MusicBrainzMetadataProvider::fetch_album(Album& album) {
        auto disc_id = generate_id(album);
        ESP_LOGI(LOG_TAG, "Disc ID = %s", disc_id.c_str());

        WiFiClient client;
        HTTPClient http;
        const std::string host = "http://musicbrainz.org/ws/2/discid/";
        const std::string query = "?inc=recordings+artist-credits&fmt=json";

        std::string url = host + disc_id + query;

        http.begin(client, url.c_str());
        ESP_LOGV(LOG_TAG, "Query: %s", url.c_str());
        int response = http.GET();
        if(response == HTTP_CODE_OK) {
            JsonDocument response;
            DeserializationError error = deserializeJson(response, http.getStream(), DeserializationOption::NestingLimit(50));

            if (error) {
                ESP_LOGE(LOG_TAG, "Parse error: %s", error.c_str());
            } else {
                if(response["releases"].is<JsonArray>()) {
                    auto releases = response["releases"].as<JsonArray>();
                    if(releases.size() != 1) {
                        ESP_LOGW(LOG_TAG, "%i releases found, can't proceed", releases.size());
                    } else {
                        auto info = releases[0].as<JsonObject>();
                        if(album.title.empty() && info["title"].is<JsonString>()) {
                            album.title = info["title"].as<std::string>();
                        }
                        if(album.artist.empty()) album.artist = MBJsonToArtistName(info);

                        // now on to find which CD of the release this is
                        if(info["media"].is<JsonArray>()) {
                            auto media = info["media"].as<JsonArray>();
                            int mediaIdx = -1;
                            for(int i = 0; i < media.size(); i++) {
                                if(media[i]["discs"].is<JsonArray>()) {
                                    for(auto disc: media[i]["discs"].as<JsonArray>()) {
                                        if(disc["id"].is<JsonString>()) {
                                            if(disc_id == disc["id"].as<std::string>()) {
                                                mediaIdx = i;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }

                            if(mediaIdx != -1 && info["media"][mediaIdx]["tracks"].is<JsonArray>()) {
                                auto tracks = info["media"][mediaIdx]["tracks"].as<JsonArray>();

                                for(int i = 0; i < std::min(tracks.size(), album.tracks.size()); i++) {
                                    if(album.tracks[i].title.empty())
                                        album.tracks[i].title = tracks[i]["title"].as<std::string>();
                                    if(album.tracks[i].artist.empty()) {
                                        const std::string artist = MBJsonToArtistName(tracks[i]);
                                        if(artist != album.artist)
                                            album.tracks[i].artist = artist;
                                    }
                                }
                            } else {
                                ESP_LOGW(LOG_TAG, "MediaIdx %i has no tracks!", mediaIdx);
                            }
                        } else {
                            ESP_LOGW(LOG_TAG, "No media in result!");
                        }

                        ESP_LOGV(LOG_TAG, "RESULT: %s by %s", album.title.c_str(), album.artist.c_str());
                        for(int i = 0; i < album.tracks.size(); i++) {
                            ESP_LOGV(LOG_TAG, "  [%02i] %s - %s", i, album.tracks[i].artist.c_str(), album.tracks[i].title.c_str());
                        }
                    }
                } else {
                    ESP_LOGW(LOG_TAG, "No releases key in response");
                }
            }
        } 
        else {
            ESP_LOGE(LOG_TAG, "HTTP error %i", response);
        }

        http.end();
    }

    const std::string MusicBrainzMetadataProvider::generate_id(const Album& album) {
        // Ref. https://musicbrainz.org/doc/Disc_ID_Calculation

        mbedtls_sha1_context ctx;
        mbedtls_sha1_init(&ctx);
        mbedtls_sha1_starts_ret(&ctx);

        char temp[16] = { 0 };
        unsigned char sha[20] = { 0 };
        size_t dummy;

        sprintf(temp, "%02X", album.tracks[0].disc_position.number);
        mbedtls_sha1_update_ret(&ctx, (unsigned char*) temp, strlen(temp));

        sprintf(temp, "%02X", album.tracks.back().disc_position.number);
        mbedtls_sha1_update_ret(&ctx, (unsigned char*) temp, strlen(temp));

        sprintf(temp, "%08X", MSF_TO_FRAMES(album.lead_out));
        mbedtls_sha1_update_ret(&ctx, (unsigned char*) temp, strlen(temp));

        for (int i = 0; i < 99; i++) {
            if(i < album.tracks.size()) {
                sprintf(temp, "%08X", MSF_TO_FRAMES(album.tracks[i].disc_position.position));
                mbedtls_sha1_update_ret(&ctx, (unsigned char*) temp, strlen(temp));
            }
            else {
                mbedtls_sha1_update_ret(&ctx, (unsigned char*) "00000000", 8);
            }
        }

        mbedtls_sha1_finish_ret(&ctx, sha);
        mbedtls_sha1_free(&ctx);

        char * mbid = rfc822_binary(sha, 20, &dummy);
        ESP_LOGV(LOG_TAG, "Disc ID = %s", mbid);

        // use it like https://musicbrainz.org/ws/2/discid/F9XV7VTnpufEOtGcaHcitqk0k2s-?inc=recordings&fmt=json
        const std::string rslt = std::string(mbid);
        free(mbid);
        return rslt;
    }
};