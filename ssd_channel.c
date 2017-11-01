/* Copyright 2009, 20 Brendan Tauras */

/* ssd_channel.cpp is part of FlashSim. */

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

/* Channel class
 * Brendan Tauras 20-08-09
 *
 * Single bus channel
 * Simulate multiple devices on 1 bus channel with variable bus transmission
 * durations for data and control delays with the Channel class.  Provide the
 * delay times to send a control signal or 1 page of data across the bus
 * channel, the bus table size for the maximum number channel transmissions that
 * can be queued, and the maximum number of devices that can connect to the bus.
 * To elaborate, the table size is the size of the channel scheduling table that
 * holds start and finish times of events that have not yet completed in order
 * to determine where the next event can be scheduled for bus utilization.
 */

#include "ssd.h"



/* a single channel bus: all connected devices share the same channel
 * simulates control and data
 * enable signals are implicitly simulated by the sender locking the bus
 *  then sending to multiple devices
 * the table size is synonymous to the queue size for the channel
 * it is not necessary to use the max connections properly, but it is provided
 *  to help ensure correctness */
Channel *ssd_channel_init(Channel *c, int64 ctrl_delay, int64 data_delay, uint table_size, uint max_connections)
{
    uint i;

    c->table_size = table_size;
    c->lock_time = (int64 *)ssd_malloc (table_size *sizeof(int64));
    c->unlock_time = (int64 *)ssd_malloc (table_size *sizeof(int64));
    c->table_entries = 0;
    c->selected_entry = 0;
    c->num_connected = 0;
    c->max_connections = max_connections;
    c->ctrl_delay = ctrl_delay;
    c->data_delay = data_delay;
    c->table_entries = 0;

    if(c->ctrl_delay < 0){
        SSD_ERROR("Bus channel warning: constructor received negative control delay value\n\tsetting control delay to 0\n");
        c->ctrl_delay = 0;
    }
    if(c->data_delay < 0){
        SSD_ERROR("Bus channel warning: constructor received negative data delay value\n\tsetting data delay to 0\n");
        c->data_delay = 0;
    }

    /* initialize scheduling tables
     * arrays allocated in initializer list */
    if(c->lock_time == NULL || c->unlock_time == NULL) {
        SSD_ERROR("Bus channel error: %s: constructor unable to allocate channel scheduling tables\n", __func__);
        ssd_bug(MEM_ERR);
    }
    for(i = 0; i < c->table_size; i++) {
        c->lock_time[i] = BUS_CHANNEL_FREE_FLAG;
        c->unlock_time[i] = BUS_CHANNEL_FREE_FLAG;
    }

    return c;
}

Channel *ssd_channel_new(int64 ctrl_delay, int64 data_delay, uint table_size, uint max_connections)
{
    SSD_NEW (Channel);
    return ssd_channel_init (obj, ctrl_delay, data_delay, table_size, max_connections);
}
/* free allocated bus channel state space */
void ssd_channel_free(Channel *c)
{
    assert(c->lock_time != NULL && c->unlock_time != NULL);

    ssd_free (c->lock_time);
    ssd_free (c->unlock_time);
    if(c->num_connected > 0)
        SSD_ERROR("Bus channel warning: %s: %d connected devices when bus channel terminated\n", __func__, c->num_connected);
    ssd_free (c);
}

/* not required before calling lock()
 * but should be used to help ensure correctness
 * controller that talks on all channels should not connect/disconnect
 *  only components that receive a single channel should connect */
enum status ssd_channel_connect(Channel *c)
{
    if(c->num_connected < c->max_connections) {
        c->num_connected++;
        return SUCCESS;
    } else {
        SSD_ERROR("Bus channel error: %s: device attempting to connect to channel when %d max devices already connected\n", __func__, c->max_connections);
        return FAILURE;
    }
}

/* not required when finished
 * but should be used to help ensure correctness
 * controller that talks on all channels should not connect/disconnect
 *  only components that receive a single channel should connect */
enum status ssd_channel_disconnect(Channel *c)
{
    if(c->num_connected > 0) {
        c->num_connected--;
        return SUCCESS;
    }
    SSD_ERROR("Bus channel error: %s: device attempting to disconnect from bus channel when no devices connected\n", __func__);
    return FAILURE;
}

/* lock bus channel for event
 * updates event with bus delay and bus wait time if there is wait time
 * bus will automatically unlock after event is finished using bus
 * event is sent across bus as soon as bus channel is available
 * event may fail if bus channel is saturated so check return value
 */
enum status ssd_channel_lock2(Channel *c, int64 start_time, int64 duration, Event *event)
{
    uint i = 0;
    int64 sched_time = BUS_CHANNEL_FREE_FLAG;

/* TODO: Recombine assert statements */
    assert(c->lock_time != NULL && c->unlock_time != NULL);
    assert(c->num_connected <= c->max_connections);
    assert(c->ctrl_delay >= 0);
    assert(c->data_delay >= 0);
    assert(start_time >= 0);
    assert(duration >= 0);

#ifndef NDEBUG
    uint j;
    SSD_DEBUG("%s", "Table entries before unlock()\n");
    for(j = 0; j < c->table_size; j++)
        SSD_DEBUG("%"FMT_64", %"FMT_64"\n", c->lock_time[j], c->unlock_time[j]);
#endif

    /* free up any table slots and sort existing ones */
    ssd_channel_unlock(c, start_time);

#ifndef NDEBUG
    SSD_DEBUG("Table entries after unlock()\n");
    for(j = 0; j < c->table_size; j++)
        SSD_DEBUG("%"FMT_64", %"FMT_64"\n", c->lock_time[j], c->unlock_time[j]);
#endif

    /* give up if no free table slots */
    if(c->table_entries >= c->table_size) {
        ssd_event_incr_time_taken(event, BLOCK_ERASE_DELAY * 2);
        for(i = 0; i < c->table_size; i++) {
            c->lock_time[i] = BUS_CHANNEL_FREE_FLAG;
            c->unlock_time[i] = BUS_CHANNEL_FREE_FLAG;
        }
        c->table_entries = 0;
        return FAILURE;
    }


    /* just schedule if table is empty */
    if(c->table_entries == 0)
        sched_time = start_time;

    /* check if can schedule before or in between before just scheduling
     * after all other events */
    else {
        /* skip over empty table entries
         * empty table entries will be first from sorting (in unlock method)
         * because the flag is a negative value */
        while(c->lock_time[i] == BUS_CHANNEL_FREE_FLAG && i < c->table_size)
            i++;

        /* schedule before first event in table */
        if(c->lock_time[i] > start_time && c->lock_time[i] - start_time >= duration)
            sched_time = start_time;

        /* schedule in between other events in table */
        if(sched_time == BUS_CHANNEL_FREE_FLAG) {
            for(; i < c->table_size - 2; i++) {
                /* enough time to schedule in between next two events */
                if(c->unlock_time[i] >= start_time  && c->lock_time[i + 1] - c->unlock_time[i] >= duration) {
                    sched_time = c->unlock_time[i];
                    break;
                }
            }
        }

        /* schedule after all events in table */
        if(sched_time == BUS_CHANNEL_FREE_FLAG)
            sched_time = c->unlock_time[c->table_size - 1];
    }

    /* write scheduling info in free table slot */
    c->lock_time[0] = sched_time;
    c->unlock_time[0] = sched_time + duration;
    c->table_entries++;

    /* update event times for bus wait and time taken */
    ssd_event_incr_bus_wait_time(event, sched_time - start_time);
    ssd_event_incr_time_taken(event, sched_time - start_time);

    return SUCCESS;
}
enum status ssd_channel_lock(Channel *c, int64 start_time, int64 duration, Event *event)
{
    uint i = 0;
    int64 sched_time = BUS_CHANNEL_FREE_FLAG;

/* TODO: Recombine assert statements */
    assert(c->lock_time != NULL && c->unlock_time != NULL);
    assert(c->num_connected <= c->max_connections);
    assert(c->ctrl_delay >= 0);
    assert(c->data_delay >= 0);
    assert(start_time >= 0);
    assert(duration >= 0);

    if(c->lock_time[0] == BUS_CHANNEL_FREE_FLAG) {
        c->lock_time[0] = start_time;
        c->unlock_time[0] = start_time;
    }
    sched_time = c->unlock_time[0];
    if (sched_time < start_time)
        sched_time = start_time;
    c->lock_time[0] = sched_time;
    c->unlock_time[0] = sched_time + duration;

    /* update event times for bus wait and time taken */
    ssd_event_incr_bus_wait_time(event, sched_time - start_time);
    ssd_event_incr_time_taken(event, sched_time - start_time);

    return SUCCESS;
}

/* remove all expired entries (finish time is less than provided time)
 * update current number of table entries used
 * sort table by finish times (2nd row) */
void ssd_channel_unlock(Channel *c, int64 start_time)
{
    uint i;

    /* remove expired channel lock entries */
    for(i = 0; i < c->table_size; i++) {
        if(c->unlock_time[i] != BUS_CHANNEL_FREE_FLAG) {
            if(c->unlock_time[i] <= start_time) {
                c->lock_time[i] = c->unlock_time[i] = BUS_CHANNEL_FREE_FLAG;
                c->table_entries--;
            }
        }
    }

    /* sort both arrays together - e.g. sort by first array but perform same
     * move operation on both arrays */
    quicksort(c->lock_time, c->unlock_time, 0, c->table_size - 1);
    return;
}
