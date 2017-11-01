
#ifndef _SSDSIM_H
#define _SSDSIM_H

#include <linux/types.h>

enum event_type{SSD_READ, SSD_WRITE, SSD_ERASE, SSD_MERGE};

extern const uint SSD_SIZE;
extern const uint PACKAGE_SIZE;
extern const uint DIE_SIZE;
extern const uint PLANE_SIZE;
extern const uint SSD_BLOCK_SIZE;


struct Ssd;
/* The SSD is the single main object that will be created to simulate a real
 * SSD.  Creating a SSD causes all other objects in the SSD to be created.  The
 * event_arrive method is where events will arrive from DiskSim. */
struct Ssd *ssd_sim_new(uint ssd_size);
void ssd_sim_free (struct Ssd *s);
/* ns*/
int64_t ssd_event_arrive(struct Ssd *ssd, enum event_type type, ulong logical_address, uint size, int64_t start_time);

#define LONG_TERM_TIMER
#define DEFAULT_TIMEOUT 500000

#ifdef _DEBUG
#define debug(fmt, args...) printk( KERN_ERR "ramssd(#%d,%s): " fmt, __LINE__, __func__, ##args)
#else
#define debug(fmt, args...) do{}while(0)
#endif
#define warning(fmt, args...) printk( KERN_INFO "[FLASHSIM](#%d,%s): " fmt, __LINE__, __func__, ##args)

#define serror(fmt, args...) printk( KERN_ERR "[FLASHSIM](#%d,%s): " fmt, __LINE__, __func__, ##args)

#define debug2 warning

#define NOUSE_PRIVATE

#include "settings.h"
#define SSD_PAGE_OOBSECS (SSD_PAGE_SECS+1)
#define RAM_SSD_SIZE (SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE*SSD_BLOCK_SIZE*SSD_PAGE_OOBSECS)
#define RAM_SSD_REAL_SIZE (SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE*SSD_BLOCK_SIZE*SSD_PAGE_SECS)

#endif
