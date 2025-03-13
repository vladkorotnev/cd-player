#pragma once

#ifndef FS_MOUNT_POINT
#define FS_MOUNT_POINT "/mnt"
#endif

#ifndef META_CACHE_PREFIX
#define META_CACHE_PREFIX FS_MOUNT_POINT "/cddb"
#endif

#ifndef FONT_DIR_PREFIX
#define FONT_DIR_PREFIX FS_MOUNT_POINT "/font"
#endif

#ifdef OTA_FVU_PASSWORD_HASH
#define OTA_FVU_ENABLED 1
#endif
