#include <esper-core/wlan.h>
#include <esper-core/prefs.h>
#include <WiFi.h>

static char LOG_TAG[] = "WLAN";
static bool initial_connection_succeeded = false;
static bool initial_connection_ongoing = false;
static bool reconnecting = false;
static bool has_ip = false;
static bool is_set_up = false;

static std::string ssid = "";
static std::string pass = "";

#ifndef WIFI_NAME
#define WIFI_NAME ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

static const Prefs::Key<std::string> PREFS_KEY_WIFI_NAME{"wifi_ssid", WIFI_NAME};
static const Prefs::Key<std::string> PREFS_KEY_WIFI_PASS{"wifi_pass", WIFI_PASS};

namespace Core::Services {
    namespace WLAN {
        static void save_current_network() {
            ESP_LOGD(LOG_TAG, "Save ssid=[%s] pass=[%s]", ssid.c_str(), pass.c_str());
            Prefs::set(PREFS_KEY_WIFI_NAME, ssid);
            Prefs::set(PREFS_KEY_WIFI_PASS, pass);
        }

        static void wifi_event(WiFiEvent_t ev) {
            switch(ev) {
                case SYSTEM_EVENT_STA_DISCONNECTED:
                    ESP_LOGE(LOG_TAG, "STA_DISCONNECTED");
                    // prevent endless loop when invalid credentials are stuck in nvram
                    initial_connection_succeeded = false;
                    initial_connection_ongoing = false;
                    WiFi.disconnect();
                    break;
                case SYSTEM_EVENT_STA_GOT_IP:
                    ESP_LOGI(LOG_TAG, "Connection succeeded. SSID:'%s', IP: %s", network_name().c_str(), current_ip().c_str());
                    WiFi.setSleep(false);
        
                    has_ip = true;
                    if(initial_connection_ongoing) {
                        initial_connection_succeeded = true;
                        initial_connection_ongoing = false;
                    } else {
                        save_current_network();
                    }
                    ESP_LOGI(LOG_TAG, "RSSI: %i dB", rssi());
                    break;
                default:
                    ESP_LOGW(LOG_TAG, "Unhandled event %i", ev);
                    break;
            }
        }
        
        const std::string chip_id() {
            uint64_t macAddress = ESP.getEfuseMac();
            uint32_t id         = 0;
            
            for (int i = 0; i < 17; i = i + 8) {
                id |= ((macAddress >> (40 - i)) & 0xff) << i;
            }
            return std::to_string(id);
        }        

        void start() {
            ssid = Prefs::get(PREFS_KEY_WIFI_NAME);
            pass = Prefs::get(PREFS_KEY_WIFI_PASS);
            initial_connection_succeeded = false;

            if(!is_set_up) {
                WiFi.persistent(false);
                WiFi.onEvent(wifi_event);
                is_set_up = true;
            }
            
            if(WiFi.status() != WL_CONNECTED) {
                if(!ssid.empty()) {
                    ESP_LOGI(LOG_TAG, "Attempt initial connection to saved network [%s]", ssid.c_str());

                    initial_connection_ongoing = true;
                    WiFi.mode(WIFI_MODE_STA);
                    WiFi.begin(ssid.c_str(), pass.c_str());
                    WiFi.waitForConnectResult();
                    WiFi.setSleep(false);
                    WiFi.setTxPower(wifi_power_t::WIFI_POWER_19_5dBm);
                    ESP_LOGI(LOG_TAG, "Power mode = %i", WiFi.getTxPower());
                } else {
                    ESP_LOGW(LOG_TAG, "No saved network");
                }
            }
        }

        void stop() {
            WiFi.disconnect();
            WiFi.mode(WIFI_OFF);
        }

        bool is_up() {
            return (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED && has_ip);
        }

        const std::string current_ip() {
            if(WiFi.getMode() == WIFI_STA)
                return WiFi.localIP().toString().c_str();
            else if(WiFi.getMode() == WIFI_AP)
                return WiFi.softAPIP().toString().c_str();
            else
                return "0.0.0.0";
        }

        const std::string network_name() {
            return ssid;
        }
        const std::string password() {
            return pass;
        }

        bool connect(const std::string& s, const std::string& p) {
            ssid = s;
            pass = p;
            reconnecting = true;
            has_ip = false;
            WiFi.disconnect(false, true);
            WiFi.mode(WIFI_MODE_STA);
            wl_status_t rslt;
            WiFi.begin(ssid.c_str(), pass.c_str());
            for (int i = 0; i < 30; ++i) { // 30 attempts, ~3-10 seconds
                rslt = WiFi.status();
                if (rslt == WL_CONNECTED && has_ip) {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for 1 second
            }
            if(rslt != WL_CONNECTED || !has_ip) {
                ESP_LOGE(LOG_TAG, "WiFi connection error (%i), has_ip = %s", (int)rslt, (has_ip ? "YES" : "NO"));
                return false;
            }
            WiFi.setTxPower(wifi_power_t::WIFI_POWER_19_5dBm);
            ESP_LOGI(LOG_TAG, "Power mode = %i", WiFi.getTxPower());
            WiFi.setSleep(false);
            return true;
        }

        int rssi() {
            return is_up() ? WiFi.RSSI() : 1;
        }

        std::vector<ScannedNetwork> scan_networks() {
            std::vector<ScannedNetwork> networks;
            ESP_LOGI(LOG_TAG, "Scanning WiFi networks...");

            // Perform a scan for available WiFi networks
            int n = WiFi.scanNetworks();
            if (n == 0) {
                ESP_LOGW(LOG_TAG, "No networks found");
            } else {
                ESP_LOGI(LOG_TAG, "%d networks found:", n);
                for (int i = 0; i < n; ++i) {
                    ScannedNetwork net;
                    net.ssid = WiFi.SSID(i).c_str();
                    net.rssi = WiFi.RSSI(i);
                    net.open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
                    networks.push_back(net);
                    ESP_LOGI(LOG_TAG, " + %s (%d) %s", net.ssid.c_str(), net.rssi, net.open ? "Open" : "Protected");
                }
            }
            WiFi.scanDelete();
            // sort networks by RSSI, from strongest to weakest
            std::sort(networks.begin(), networks.end(), [](const ScannedNetwork& a, const ScannedNetwork& b) {
                return a.rssi > b.rssi;
            });

            return networks;
        }
    }
}
