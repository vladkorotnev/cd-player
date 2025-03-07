#pragma once
#include <scrobbler.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <MD5Builder.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esper-cdp/utils.h>

// NB: most of this file is AI-generated cause I was too lazy. Todo refactor someday? But it works!

class LastFMScrobbler : public Scrobbler {
public:
    LastFMScrobbler(const std::string& api_key, const std::string& api_secret, const std::string& username, const std::string& password) :
        api_key(api_key),
        api_secret(api_secret),
        username(username),
        password(password)
    {
        session_key = get_session_key();
        if (session_key.empty()) {
            ESP_LOGE(LOG_TAG, "Failed to get session key!");
        } else {
            ESP_LOGI(LOG_TAG, "Got session key: %s", session_key.c_str());
        }
    }

protected:
    void do_scrobble(const CD::Track& meta) override {
        if (session_key.empty()) {
            ESP_LOGE(LOG_TAG, "No session key for scrobbling!");
            return;
        }

        time_t now;
        time(&now);

        std::string post_data = "method=track.scrobble&artist=" + urlEncode(meta.artist) +
                                "&track=" + urlEncode(meta.title) +
                                "&timestamp=" + std::to_string(now) +
                                "&trackNumber=" + std::to_string(meta.disc_position.number) +
                                "&api_key=" + api_key +
                                "&sk=" + session_key;

        std::string api_sig = calculate_api_sig("api_key" + api_key + "artist" + meta.artist + "methodtrack.scrobble" + "sk" + session_key + "timestamp" + std::to_string(now) + "track" + meta.title + "trackNumber" + std::to_string(meta.disc_position.number) + api_secret);

        perform_lastfm_request(post_data, api_sig, "Scrobbling");
    }

    void do_now_playing(const CD::Track& meta) override {
        if (session_key.empty()) {
            ESP_LOGE(LOG_TAG, "No session key for now playing!");
            return;
        }

        std::string post_data = "method=track.updateNowPlaying&artist=" + urlEncode(meta.artist) +
                                "&track=" + urlEncode(meta.title) +
                                "&trackNumber=" + std::to_string(meta.disc_position.number) +
                                "&api_key=" + api_key +
                                "&sk=" + session_key;

        std::string api_sig = calculate_api_sig("api_key" + api_key + "artist" + meta.artist + "methodtrack.updateNowPlaying" + "sk" + session_key + "track" + meta.title + "trackNumber" + std::to_string(meta.disc_position.number) + api_secret);

        perform_lastfm_request(post_data, api_sig, "Now Playing");
    }

private:
    const char * LOG_TAG = "LastFM";
    std::string api_key;
    std::string api_secret;
    std::string username;
    std::string password;
    std::string session_key;
    const char * url = "http://ws.audioscrobbler.com/2.0/";

    void perform_lastfm_request(std::string& post_data, const std::string& api_sig, const std::string& action_description) {
        HTTPClient http;
        http.setTimeout(3000);
        http.begin(url);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");

        post_data += "&api_sig=" + api_sig + "&format=json";

        ESP_LOGD(LOG_TAG, "%s to: %s\n%s", action_description.c_str(), url, post_data.c_str());

        int httpResponseCode = http.POST(post_data.c_str());
        if (httpResponseCode == 200) {
            ESP_LOGI(LOG_TAG, "%s: HTTP 200 OK", action_description.c_str());
            ESP_LOGV(LOG_TAG, "[...] %s", http.getString().c_str());
        } else {
            ESP_LOGE(LOG_TAG, "%s failed with code: %d", action_description.c_str(), httpResponseCode);
            ESP_LOGE(LOG_TAG, "[...] %s", http.getString().c_str());
        }
        http.end();
    }

    std::string get_session_key() {
        std::string post_data = "method=auth.getMobileSession&username=" + username +
            "&password=" + password +
            "&api_key=" + api_key;

        std::string api_sig = calculate_api_sig("api_key" + api_key + "methodauth.getMobileSession" + "password" + password + "username" + username + api_secret);
        JsonDocument doc = perform_lastfm_get_response(post_data, api_sig, "AuthToken");
        
        if(doc.isNull()) {
            ESP_LOGE(LOG_TAG, "Null response");
            return "";
        }
        
        if(doc["session"]["key"].is<JsonString>())
           return doc["session"]["key"].as<std::string>();

        ESP_LOGE(LOG_TAG, "No session.key in object!");

        return "";
    }

    JsonDocument perform_lastfm_get_response(std::string& post_data, const std::string& api_sig, const std::string& action_description) {
        HTTPClient http;
        http.setTimeout(3000);
        http.begin(url);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");

        post_data += "&api_sig=" + api_sig + "&format=json";

        ESP_LOGD(LOG_TAG, "%s POST to: %s\n%s", action_description.c_str(), url, post_data.c_str());
        JsonDocument doc;
        int httpResponseCode = http.POST(post_data.c_str());
        if (httpResponseCode == 200) {
           ESP_LOGI(LOG_TAG, "%s: HTTP 200 OK", action_description.c_str());
           
           DeserializationError error = deserializeJson(doc, http.getStream());
           if (error) {
                ESP_LOGE(LOG_TAG, "deserializeJson() failed: %s", error.c_str());
           }
        } else {
            ESP_LOGE(LOG_TAG, "Failed to %s with code: %d", action_description.c_str(), httpResponseCode);
            ESP_LOGE(LOG_TAG, "[...] %s", http.getString().c_str());
        }
        http.end();
        return doc;
    }

    std::string calculate_api_sig(const std::string& str_to_hash) {
        MD5Builder md5;
        md5.begin();
        md5.add(str_to_hash.c_str());
        md5.calculate();
        return md5.toString().c_str();
    }
};
