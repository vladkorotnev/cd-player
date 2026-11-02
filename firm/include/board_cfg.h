#pragma once

#ifdef BOARD_TYPE_OG8M
    #include "boards/og8m.h"
#else
    #error "Board type not defined or unknown"
#endif


// ---- Safe defaults for optional fields ----
#ifndef BCONF_SERIAL_BAUD
    #define BCONF_SERIAL_BAUD 115200
#endif
#ifndef BCONF_BYPASS_RELAY_PIN
    #define BCONF_BYPASS_RELAY_PIN GPIO_NUM_NC
#endif