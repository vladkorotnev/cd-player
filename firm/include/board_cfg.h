#pragma once

#ifdef BOARD_TYPE_OG8M
    #include "boards/og8m.h"
#else
    #error "Board type not defined or unknown"
#endif

#ifndef BCONF_SERIAL_BAUD
    #define BCONF_SERIAL_BAUD 115200
#endif