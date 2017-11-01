/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#ifndef FOR_JASMINE
#include "../oftl/common.h"
#else
#define FLASHPGSZBIT 12
#define FLASHPG_NUM_BLOCK_SHIFT 6
#define BANK_GROUP_BIT 2
#define FLASHPG_SECTOR (1<<(FLASHPGSZBIT-9))
#define FLASHPGS_PER_BLOCK (1<<FLASHPG_NUM_BLOCK_SHIFT)
#define GMT_START 2

//#define NO_PERSIST

#ifdef NO_PERSIST
// 150GB 128.8GB
//#define SSD_SIZE_X (4*1024)
// 37.5GB 32.2GB
//#define SSD_SIZE_X (1024+32)
#define SSD_SIZE_X (128)
#else
#define SSD_SIZE_X (128)
#endif

#endif

#ifdef ENABLE_UPS
static inline int page_reserved (unsigned long capacity, u32 ppn)
{
    return (ppn < (1 << FLASHPG_NUM_BLOCK_SHIFT));
}
#else
static inline int page_reserved (unsigned long capacity, u32 ppn)
{
    return 0;
}
#endif


#define SSD_PAGE_SECS FLASHPG_SECTOR
#endif
