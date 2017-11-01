/* Copyright 2009, 20 Brendan Tauras */

/* ssd_ssd.cpp is part of FlashSim. */

/* FlashSim is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version. */

/* FlashSim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. */

/* You should have received a copy of the GNU General Public License
 * along with FlashSim.  If not, see <http://www.gnu.org/licenses/>. */

/****************************************************************************/

/* Ssd class
 * Brendan Tauras 2009-11-03
 *
 * The SSD is the single main object that will be created to simulate a real
 * SSD.  Creating a SSD causes all other objects in the SSD to be created.  The
 * event_arrive method is where events will arrive from DiskSim. */

#include "ssd.h"

static int _mem_size = 0;
static char *_mem = NULL;
static int _pos = 0;

#ifdef __KERNEL__
#define _malloc(x) vmalloc(x)
#define _free(x) vfree(x)
#else
#define _malloc(x) malloc(x)
#define _free(x) free(x)
#endif

#define MEM_SIZE_X PLANE_SIZE*SSD_BLOCK_SIZE/16
#define FLASHSIM_MEM_SIZE ((MEM_SIZE_X/20 + 1)*1024*1024)

static int _mem_init(long size)
{
    assert(_mem == NULL);
    SSD_DEBUG("ramssd:ssd mem size =%ld %ldKB %ld MB\n", size, size >> 10, size >> 20);
    _mem_size = size;
    _mem = _malloc(_mem_size);
    if (!_mem) {
        SSD_ERROR("mem mallocfailed\n");
        return -1;
    }

    return 0;
}

#define MEM_DEBUG
#ifdef MEM_DEBUG
static unsigned long long _CNT = 0;
#endif

static void _mem_exit(void)
{
    SSD_DEBUG("mem used=%d, total=%d %d MB\n", _pos, _mem_size, _mem_size >> 20);
    _free(_mem);
}

/* use caution when editing the initialization list - initialization actually
 * occurs in the order of declaration in the class definition and not in the
 * order listed here */

static Ssd *ssd_sim_init(Ssd *obj, uint ssd_size)
{
    uint i;

    obj->size = ssd_size;

    /* use a const pointer (Package * const data) to use as an array
     * but like a reference, we cannot reseat the pointer */
    obj->data = (Package **) ssd_malloc(ssd_size * sizeof(Package*));

    /* set erases remaining to BLOCK_ERASES to match Block constructor args
     *  in Plane class
     * this is the cheap implementation but can change to pass through classes */
    obj->erases_remaining = BLOCK_ERASES;

    /* assume all Planes are same so first one can start as least worn */
    obj->least_worn = 0;

    /* assume hardware created at time 0 and had an implied free erasure */
    obj->last_erase_time = 0;


    ssd_bus_init(&obj->bus, obj->size, BUS_CTRL_DELAY, BUS_DATA_DELAY, BUS_TABLE_SIZE, BUS_MAX_CONNECT);

    /* new cannot initialize an array with constructor args so
     *      malloc the array
     *      then use placement new to call the constructor for each element
     * chose an array over container class so we don't have to rely on anything
     *  i.e. STL's std::vector */
    /* array allocated in initializer list:
     * data = (Package *) malloc(ssd_size * sizeof(Package)); */
    if(obj->data == NULL){
        SSD_ERROR("Ssd error: %s: constructor unable to allocate Package data\n", __func__);
        ssd_bug(MEM_ERR);
    }
    for (i = 0; i < ssd_size; i++) {
        obj->data[i] = ssd_package_new(obj, ssd_bus_get_channel(&obj->bus, i), PACKAGE_SIZE);
    }

    return obj;
}

Ssd *ssd_sim_new(uint ssd_size)
{
    Ssd *obj;
    if (_mem_init(FLASHSIM_MEM_SIZE) < 0)
      return NULL;

    obj = ssd_malloc (sizeof(Ssd));
    if (!obj)
        return NULL;

     ssd_sim_init (obj, ssd_size);
#ifdef MME_DEBUG
     SSD_DEBUG("cnt=%llu, _pos=%d, total=%d\n", _CNT, _pos, _mem_size);
#endif
    return  obj;
}

void ssd_sim_free (Ssd *s)
{
    uint i;
    /* explicitly call destructors and use free
     * since we used malloc and placement new */
    for (i = 0; i < s->size; i++) {
        ssd_package_free (s->data[i]);
    }
    ssd_free(s->data);
    ssd_free (s);
    _mem_exit();
}

#include "settings.h"
static uint get_channel(Address *address)
{
#if BANK_GROUP_BIT == 0
    return address->package;
#else
#define BANK_GROUP_MASK ((1 << BANK_GROUP_BIT)-1)
    uint channel = address->package & (~BANK_GROUP_MASK);
    channel = channel | (address->page &BANK_GROUP_MASK);
    return channel;
#endif
}

/* This is the function that will be called by DiskSim
 * Provide the event (request) type (see enum in ssd.h),
 *  logical_address (page number), size of request in pages, and the start
 *  time (arrive time) of the request
 * The SSD will process the request and return the time taken to process the
 *  request.  Remember to use the same time units as in the config file. */
int64_t ssd_event_arrive(Ssd *s, enum event_type type, ulong logical_address, uint size, int64_t start_time)
{
    Event _event;
    Address _address;
    Event *event = &_event;
    Address *address = &_address;
    uint channel = 0;

    assert(start_time >= 0);
    assert((long long int) logical_address <= (long long int) SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * SSD_BLOCK_SIZE);

    ssd_event_init(event, type, logical_address, size, start_time);

    /* REAL SSD ONLY */
    if (ssd_event_get_event_type(event) != SSD_READ
        && ssd_event_get_event_type(event) != SSD_WRITE
        && ssd_event_get_event_type(event) != SSD_ERASE) {
        SSD_ERROR("Ssd error: %s: request failed:\n", __func__);
        ssd_event_print(event);
    }
    /* END REAL SSD ONLY */

    /* STUB ONLY
     * real SSD will let the FTL determine the physical address */
    address->page = logical_address % SSD_BLOCK_SIZE;
    logical_address /= SSD_BLOCK_SIZE;
    address->block = logical_address % PLANE_SIZE;
    logical_address /= PLANE_SIZE;
    address->plane = logical_address % DIE_SIZE;
    logical_address /= DIE_SIZE;
    address->die = logical_address % PACKAGE_SIZE;
    logical_address /= PACKAGE_SIZE;
    address->package = logical_address % SSD_SIZE;
    logical_address /= SSD_SIZE;
    address->valid = PAGE;

    channel = get_channel(address);
    ssd_event_set_address(event, address);

    /* the bus locking should be done in the controller in the real SSD */
    if(type == SSD_READ){
        ssd_event_incr_time_taken(event, BUS_CTRL_DELAY + BUS_DATA_DELAY);
        if(ssd_package_read(s->data[address->package], event) != SUCCESS) {
            SSD_ERROR("Ssd error: %s: read request failed:\n", __func__);
            return -1;
        } else {
            if(ssd_bus_lock(&s->bus, channel, start_time, event->time_taken, event) != SUCCESS)
                SSD_ERROR("Ssd error: %s: locking bus channel %u for read data failed:\n", __func__, address->package);
        }
    } else if(type == SSD_WRITE){
        ssd_event_incr_time_taken(event, BUS_CTRL_DELAY + BUS_DATA_DELAY);
        if(ssd_package_write(s->data[address->package],event) != SUCCESS) {
            SSD_ERROR("Ssd error: %s: write request failed:\n", __func__);
            return -1;
        } else {
            if(ssd_bus_lock(&s->bus, channel, start_time, event->time_taken, event) != SUCCESS){
                SSD_ERROR("Ssd error: %s: locking bus channel %u for write data failed:\n", __func__, address->package);
            }
        }
    } else if (ssd_event_get_event_type(event) == SSD_ERASE) {
        ssd_event_incr_time_taken(event, BUS_CTRL_DELAY + BUS_DATA_DELAY);
        if(ssd_package_erase(s->data[address->package],event) != SUCCESS) {
            SSD_ERROR("Ssd error: %s: erase request failed:\n", __func__);
            return -1;
        } else {
            if(ssd_bus_lock(&s->bus, channel, start_time, event->time_taken, event) != SUCCESS){
                SSD_ERROR("Ssd error: %s: locking bus channel %u for erase data failed:\n", __func__, address->package);
                return -1;
            }
        }
    } else {
        SSD_ERROR("Ssd error: %s: incoming request was not of type read or write\n", __func__);
        ssd_event_print(event);
    }
    /* END STUB ONLY */

    /* use start_time as a temporary for returning time taken to service event */
    start_time = ssd_event_get_time_taken(event);
    return start_time;
}


void *ssd_malloc(int x)
{
    void *p;
#ifdef MEM_DEBUG
    if (x == 0) {
        SSD_DEBUG("cnt=%llu\n", _CNT);
        return NULL;
    }
    ++_CNT;
    if (_CNT < 100 || _CNT%10000 == 0 )
        SSD_DEBUG("x=%d, cnt=%llu, _pos=%d, total=%d\n", x, _CNT, _pos, _mem_size);
#endif
    p = _mem + _pos;
    _pos += x;
    assert(_pos < _mem_size);
    return p;
}
void ssd_free(void *p)
{
}
