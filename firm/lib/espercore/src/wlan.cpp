#include <esper-core/wlan.h>
#include <WiFi.h>

static char LOG_TAG[] = "WLAN";
static bool initial_connection_succeeded = false;
static bool initial_connection_ongoing = false;
static bool reconnecting = false;
static bool has_ip = false;

static std::string ssid = "";
static std::string pass = "";

namespace Core::Services {
    namespace WLAN {
        static void ap_fallback() {
            WiFi.disconnect(false, true);
            WiFi.mode(WIFI_MODE_AP);
            ssid = "esper_" + chip_id();
            ESP_LOGI(LOG_TAG, "Starting AP");
            WiFi.softAP(ssid.c_str());
        }

        static void wifi_event(WiFiEvent_t ev) {
            switch(ev) {
                case SYSTEM_EVENT_STA_DISCONNECTED:
                    ESP_LOGI(LOG_TAG, "STA_DISCONNECTED");
                    if(!initial_connection_succeeded) {
                        ESP_LOGI(LOG_TAG, "Initial connection failed, fall back to AP mode");
                        ap_fallback();
                    } else if (reconnecting) {
                        // expect disconnect before next connection is made
                        reconnecting = false;
                    } else {
                        if(WiFi.getMode() == WIFI_OFF) return; // deliberate power off
                        ESP_LOGI(LOG_TAG, "Connection lost, reconnecting to %s in a few seconds...", ssid.c_str());
                        vTaskDelay(pdMS_TO_TICKS(5000));
                        WiFi.mode(WIFI_MODE_STA);
                        WiFi.begin(ssid.c_str(), pass.c_str());
                    }
                    break;
                case SYSTEM_EVENT_STA_GOT_IP:
                    ESP_LOGI(LOG_TAG, "Connection succeeded. SSID:'%s', IP: %s", network_name().c_str(), current_ip().c_str());
                    WiFi.setSleep(false);
        
                    has_ip = true;
                    if(initial_connection_ongoing) {
                        initial_connection_succeeded = true;
                    } else {
                        // TODO: save_current_network();
                    }
                    ESP_LOGI(LOG_TAG, "RSSI: %i dB", rssi());
                    break;
                default:
                    ESP_LOGI(LOG_TAG, "Unhandled event %i", ev);
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
            // TODO load from settings
            initial_connection_succeeded = false;

            WiFi.persistent(false);
            WiFi.onEvent(wifi_event);
            
            if(WiFi.status() != WL_CONNECTED) {
                if(true) {
                    ESP_LOGI(LOG_TAG, "Attempt initial connection to saved network...");

                    ssid = WIFI_NAME;
                    pass = WIFI_PASS;

                    initial_connection_ongoing = true;
                    WiFi.mode(WIFI_MODE_STA);
                    WiFi.begin(ssid.c_str(), pass.c_str());
                    WiFi.waitForConnectResult();
                    WiFi.setSleep(false);
                    WiFi.setTxPower(wifi_power_t::WIFI_POWER_19_5dBm);
                    ESP_LOGI(LOG_TAG, "Power mode = %i", WiFi.getTxPower());
                } else {
                    ESP_LOGI(LOG_TAG, "No saved network, use AP fallback");
                    ap_fallback();
                }
            }
        }

        void stop() {
            WiFi.disconnect();
            WiFi.mode(WIFI_OFF);
        }

        bool is_up() {
            return (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED && has_ip)
                || is_softAP();
        }

        bool is_softAP() {
            return (WiFi.getMode() == WIFI_AP && ((WiFi.getStatusBits() & AP_STARTED_BIT) != 0));
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

        void connect(const char * s, const char * p) {
            ssid = std::string(s);
            pass = std::string(p);
            reconnecting = true;
            has_ip = false;
            WiFi.disconnect(false, true);
            WiFi.mode(WIFI_MODE_STA);
            wl_status_t rslt = WiFi.begin(s, p);
            if(rslt != WL_CONNECTED) {
                ESP_LOGI(LOG_TAG, "WiFi connection error (%i): fallback to SoftAP", (int)rslt);
                ap_fallback();
            }
            WiFi.setTxPower(wifi_power_t::WIFI_POWER_19_5dBm);
            ESP_LOGI(LOG_TAG, "Power mode = %i", WiFi.getTxPower());
            WiFi.setSleep(false);
        }

        int rssi() {
            return is_softAP() ? 1 : WiFi.RSSI();
        }
    }
}