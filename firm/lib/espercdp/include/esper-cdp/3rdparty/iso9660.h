/****************************************************************************
 * ISO9660 devoptab
 * 
 * Copyright (C) 2008-2010
 * tipoloski, clava, shagkur, Tantric, joedj
 ****************************************************************************/
// Altered for the ESPer Project - Akasaka, 2025/02

 #ifndef __ISO9660_H__
 #define __ISO9660_H__

 #include <esper-cdp/atapi.h>

 #define ISO_MAXPATHLEN		128

 
 bool ISO9660_Mount(ATAPI::Device* disc_interface);
 bool ISO9660_Unmount();
 const char *ISO9660_GetVolumeLabel();

 
 #endif