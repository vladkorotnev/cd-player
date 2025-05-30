#include "modes/ftp_mode.h"
#include "ftp_mode/browser.h"
#include <mode_host.h>

static const char LOG_TAG[] = "FTPMode";

FTPMode::FTPMode(const PlatformSharedResources res, ModeHost * host):
    stopEject (resources.keypad, (1 << 0)),
    playPause (resources.keypad, (1 << 1)),
    rewind (resources.keypad, (1 << 2)),
    forward (resources.keypad, (1 << 3)),
    prevTrackDisc (resources.keypad, (1 << 4)),
    nextTrackDisc (resources.keypad, (1 << 5)),
    playMode (resources.keypad, (1 << 6)),
    rootView(resources, std::make_shared<FTPBrowser>(resources.router), {EGPointZero, {160, 32}}),
    Mode(res, host) {
}
