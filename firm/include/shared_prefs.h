#pragma once
#include <esper-core/prefs.h>
#include <string>

/// Whether to show CD lyrics or not
static const Prefs::Key<bool> PREFS_KEY_CD_LYRICS_ENABLED {"cd_lyrics", true};

/// CDDB server address
static const Prefs::Key<std::string> PREFS_KEY_CDDB_ADDRESS {"cddb_srv", "gnudb.gnudb.org"};
/// CDDB server auth email. Note that GnuDB prohibits anonymous explicitly.
static const Prefs::Key<std::string> PREFS_KEY_CDDB_EMAIL {"cddb_email", "esper-cdp@esper.genjit.su"};