#pragma once
#include <esper-core/prefs.h>
#include <string>

/// Whether to include CD mode in the mode cycle button
static const Prefs::Key<bool> PREFS_KEY_CD_MODE_INCLUDED {"cd_mode_in", true};
/// Whether to include Radio mode in the mode cycle button
static const Prefs::Key<bool> PREFS_KEY_RADIO_MODE_INCLUDED {"ra_mode_in", true};
/// Whether to include BT mode in the mode cycle button
static const Prefs::Key<bool> PREFS_KEY_BLUETOOTH_MODE_INCLUDED {"bt_mode_in", true};

/// Whether to show CD lyrics or not
static const Prefs::Key<bool> PREFS_KEY_CD_LYRICS_ENABLED {"cd_lyrics", true};
/// Whether to cache CD metadata or not
static const Prefs::Key<bool> PREFS_KEY_CD_CACHE_META {"cd_cache_meta", true};
/// Whether to use MusicBrainz
static const Prefs::Key<bool> PREFS_KEY_CD_MUSICBRAINZ_ENABLED {"cd_meta_mb", true};
/// Whether to use CDDB
static const Prefs::Key<bool> PREFS_KEY_CD_CDDB_ENABLED {"cd_meta_cddb", true};
/// Whether to use LRCLib
static const Prefs::Key<bool> PREFS_KEY_CD_LRCLIB_ENABLED {"cd_meta_lrclib", true};
/// Whether to use QQ Music lyrics
static const Prefs::Key<bool> PREFS_KEY_CD_QQ_ENABLED {"cd_meta_qq", true};

/// Whether to scrobble to Last FM or not
static const Prefs::Key<bool> PREFS_KEY_CD_LASTFM_ENABLED {"cd_lastfm", false};
static const Prefs::Key<std::string> PREFS_KEY_CD_LASTFM_USER {"cd_lfm_user", ""};
static const Prefs::Key<std::string> PREFS_KEY_CD_LASTFM_PASS {"cd_lfm_pass", ""};

/// CDDB server address
static const Prefs::Key<std::string> PREFS_KEY_CDDB_ADDRESS {"cddb_srv", "gnudb.gnudb.org"};
/// CDDB server auth email. Note that GnuDB prohibits anonymous explicitly.
static const Prefs::Key<std::string> PREFS_KEY_CDDB_EMAIL {"cddb_email", "esper-cdp@esper.genjit.su"};

/// Bluetooth device name
static const Prefs::Key<std::string> PREFS_KEY_BT_NAME {"bt_name", "ESPer-CDP"};
/// Is BT auto connect enabled
static const Prefs::Key<bool> PREFS_KEY_BT_RECONNECT {"bt_auto_conn", true};
/// Is BT pin code enabled
static const Prefs::Key<bool> PREFS_KEY_BT_NEED_PIN {"bt_use_pin", true};
