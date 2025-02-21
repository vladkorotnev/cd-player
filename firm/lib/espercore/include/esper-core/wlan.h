#pragma once
#include <string>

namespace Core::Services {
    namespace WLAN {
        /// @brief Initialize the network service and connect to the saved network, if it's available, otherwise start an ad-hoc network
        void start();
        /// @brief Whether the network service is initialized and connected or hosting an ad-hoc network 
        bool is_up();
        bool is_softAP();

        /// @brief Current IP address string 
        const std::string current_ip();
        /// @brief Current network name string 
        const std::string network_name();

        /// @brief Connect to a specified network. If connection succeeds, the network credentials are saved into NVRAM.
        void connect(const char * ssid, const char * pass);

        /// @brief Get the active connection RSSI
        int rssi();
    }
}