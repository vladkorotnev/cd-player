#pragma once
#include <esper-core/wlan.h>

namespace Core::Services {
    namespace NTP {
        void start();
    }

    namespace Telnet {
        void start(int port = 23);
    }
}