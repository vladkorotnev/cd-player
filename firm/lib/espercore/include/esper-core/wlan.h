#pragma once
#include <string>
#include <vector>

namespace Core::Services {
    namespace WLAN {
        struct ScannedNetwork {
            std::string ssid;
            int32_t rssi;
            bool open;
        };

        /// @brief Initialize the network service and connect to the saved network, if it's available, otherwise start an ad-hoc network
        void start();
        /// @brief Stop the network service. Use e.g. before starting up Bluetooth.
        void stop();
        /// @brief Whether the network service is initialized and connected or hosting an ad-hoc network 
        bool is_up();

        /// @brief Current IP address string 
        const std::string current_ip();
        /// @brief Current network name string 
        const std::string network_name();
        const std::string password();

        /// @brief Connect to a specified network. If connection succeeds, the network credentials are saved into NVRAM.
        bool connect(const std::string& ssid, const std::string& pass);

        /// @brief Get the active connection RSSI
        int rssi();

        const std::string chip_id();
        std::vector<ScannedNetwork> scan_networks();
    }
}