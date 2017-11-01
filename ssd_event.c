/* Copyright 2009, 20 Brendan Tauras */

/* ssd_event.cpp is part of FlashSim. */

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

/* Event class
 * Brendan Tauras 20-07-16
 *
 * Class to manage I/O requests as events for the SSD.  It was designed to keep
 * track of an I/O request by storing its type, addressing, and timing.  The
 * SSD class creates an instance for each I/O request it receives.
 */

#include "ssd.h"

Event *ssd_event_init (Event *obj, enum event_type type, ulong logical_address, uint size, int64 start_time)
{
    assert(start_time >= 0);
    obj->start_time = start_time;
    obj->time_taken = 0;
    obj->bus_wait_time = 0;
    obj->type = type;
    obj->logical_address = logical_address;
    obj->size = size;
    obj->next = NULL;
    return obj;
}

#if 0
/* see "enum event_type" in ssd.h for details on event types */
Event *ssd_event_new (enum event_type type, ulong logical_address, uint size, int64 start_time)
{
    SSD_NEW(Event);
    return ssd_event_init (obj, type, logical_address, size, start_time);
}

void ssd_event_free (Event *e)
{
    ssd_free (e);
}

/* find the last event in the list to finish and use that event's finish time
 *  to calculate time_taken
 * add bus_wait_time for all events in the list to bus_wait_time
 * all events in the list do not need to start at the same time
 * bus_wait_time can potentially exceed time_taken with long event lists
 *  because bus_wait_time is a sum while time_taken is a max
 * be careful to only call this method once when the metaevent is finished */
void ssd_event_consolidate_metaevent(Event *e, Event *list)
{
    Event *cur;
    int64 max;
    int64 tmp;

    assert(e->start_time >= 0);

    /* find max time taken with respect to this event's start_time */
    max = e->start_time - list->start_time + list->time_taken;
    for(cur = list->next; cur != NULL; cur = cur->next)
    {
        tmp = e->start_time - cur->start_time + cur->time_taken;
        if(tmp > max)
            max = tmp;
        e->bus_wait_time += ssd_event_get_bus_wait_time(cur);
    }
    e->time_taken = max;
    assert(e->time_taken >= 0);
    assert(e->bus_wait_time >= 0);
    return;
}

ulong ssd_event_get_logical_address(Event *e)
{
    return e->logical_address;
}

uint ssd_event_get_size(Event *e)
{
    return e->size;
}

int64 ssd_event_get_bus_wait_time(Event *e)
{
    assert(e->bus_wait_time >= 0);
    return e->bus_wait_time;
}

Event *ssd_event_get_next(Event *e)
{
    return e->next;
}

void ssd_event_set_merge_address(Event *e, const Address *address)
{
    e->merge_address = *address;
}

void ssd_event_set_next(Event *e, Event *next)
{
    e->next = next;
}

#endif

const Address *ssd_event_get_address(Event *e)
{
    return &e->address;
}

const Address *ssd_event_get_merge_address(Event *e)
{
    return &e->merge_address;
}

enum event_type ssd_event_get_event_type(Event *e)
{
    return e->type;
}

int64 ssd_event_get_start_time(Event *e)
{
    assert(e->start_time >= 0);
    return e->start_time;
}

int64 ssd_event_get_time_taken(Event *e)
{
    assert(e->time_taken >= 0);
    return e->time_taken;
}

void ssd_event_set_address(Event *e, const Address *address)
{
    e->address = *address;
}

int64 ssd_event_incr_bus_wait_time(Event *e, int64 time_incr)
{
    if(time_incr > 0)
        e->bus_wait_time += time_incr;
    return e->bus_wait_time;
}

int64 ssd_event_incr_time_taken(Event *e, int64 time_incr)
{
    if(time_incr > 0)
        e->time_taken += time_incr;
    return e->time_taken;
}

void ssd_event_print(Event *e)
{
    char buf[512];
    char *p;
    Address *a = &e->address;
    int len = 0;
    uint ppn;

    if(e->type == SSD_READ)
        p = "Read ";
    else if(e->type == SSD_WRITE)
        p = "Write";
    else if(e->type == SSD_ERASE)
        p = "Erase";
    else if(e->type == SSD_MERGE)
        p = "Merge";
    else
        p = "Unknown event type: ";

    ppn = ((((((a->package * PACKAGE_SIZE) + a->die) * DIE_SIZE) +a->plane) *PLANE_SIZE) +a->block)*SSD_BLOCK_SIZE +a->page;
    len = snprintf(buf, sizeof(buf)-1, "%s", p);
    len += snprintf(buf+len, sizeof(buf)-len-1, "(%d, %d, %d, %d, %d, %d)", a->package, a->die, a->plane, a->block, a->page, (int) a->valid);
    //ssd_address_print (&e->address);
    if(e->type == SSD_MERGE)
        ssd_address_print (&e->merge_address);

    SSD_ERROR("%s ppn=%u %lu Time %"FMT_64"[%"FMT_64", %"FMT_64") Bus_wait: %"FMT_64"\n", buf, ppn, e->logical_address, e->time_taken, e->start_time, e->start_time + e->time_taken, e->bus_wait_time);
}

#if 0
/* may be useful for further integration with DiskSim */

/* caution: copies pointers from rhs */
ioreq_event &ssd_event_operator= (const ioreq_event &rhs)
{
    assert(&rhs != NULL);
    if((const ioreq_event *) &rhs == (const ioreq_event *) &(this->ioreq))
        return *(this->ioreq);
    ioreq->time = rhs.time;
    ioreq->type = rhs.type;
    ioreq->next = rhs.next;
    ioreq->prev = rhs.prev;
    ioreq->bcount = rhs.bcount;
    ioreq->blkno = rhs.blkno;
    ioreq->flags = rhs.flags;
    ioreq->busno = rhs.busno;
    ioreq->slotno = rhs.slotno;
    ioreq->devno = rhs.devno;
    ioreq->opid = rhs.opid;
    ioreq->buf = rhs.buf;
    ioreq->cause = rhs.cause;
    ioreq->tempint1 = rhs.tempint1;
    ioreq->tempint2 = rhs.tempint2;
    ioreq->tempptr1 = rhs.tempptr1;
    ioreq->tempptr2 = rhs.tempptr2;
    ioreq->mems_sled = rhs.mems_sled;
    ioreq->mems_reqinfo = rhs.mems_reqinfo;
    ioreq->start_time = rhs.start_time;
    ioreq->batchno = rhs.batchno;
    ioreq->batch_complete = rhs.batch_complete;
    ioreq->batch_size = rhs.batch_size;
    ioreq->batch_next = rhs.batch_next;
    ioreq->batch_prev = rhs.batch_prev;
    return *ioreq;
}
#endif
