/* Copyright 2009, 20 Brendan Tauras */

/* ssd_bus.cpp is part of FlashSim. */

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

/* Bus class
 * Brendan Tauras 2009-04-06
 *
 * Multi-channel bus comprised of Channel class objects
 * Simulates control and data delays by allowing variable channel lock
 * durations.  The sender (controller class) should specify the delay (control,
 * data, or both) for events (i.e. read = ctrl, ctrl+data; write = ctrl+data;
 * erase or merge = ctrl).  The hardware enable signals are implicitly
 * simulated by the sender locking the appropriate bus channel through the lock
 * method, then sending to multiple devices by calling the appropriate method
 * in the Package class. */

#include "ssd.h"



/* a multi-channel bus: multiple independent channels that operate in parallel
 * allocate channels and pass parameters to channels via the lock method
 * the table size is synonymous to the queue size for each separate channel
 * it is not necessary to use the max connections properly, but it is provided
 *  to help ensure correctness */
Bus *ssd_bus_init(Bus *b, uint num_channels, int64 ctrl_delay, int64 data_delay, uint table_size, uint max_connections)


    /* use a const pointer (Channel * const channels) to use as an array
     * but like a reference, we cannot reseat the pointer */

{
    uint i;
    b->num_channels = num_channels;
    b->channels = (Channel **) ssd_malloc(num_channels * sizeof(Channel*));

    assert(table_size > 0);
    if(ctrl_delay < 0){
        SSD_ERROR("Bus warning: %s: constructor received negative control delay value\n\tsetting control delay to 0\n", __func__);
        ctrl_delay = 0;
    }
    if(data_delay < 0){
        SSD_ERROR("Bus warning: %s: constructor received negative data delay value\n\tsetting data delay to 0\n", __func__);
        data_delay = 0;
    }

    /* allocate channels */
    /* new cannot initialize an array with constructor args
     *    malloc the array
     *    then use placement new to call the constructor for each element
     * chose an array over container class so we don't have to rely on anything
     *    i.e. STL's std::vector */
    /* array allocated in initializer list:
     * channels = (Channel *) malloc(num_channels * sizeof(Channel)); */
    if(b->channels == NULL) {
        SSD_ERROR("Bus error: %s: constructor unable to allocate Channels\n", __func__);
        ssd_bug(MEM_ERR);
    }
    for(i = 0; i < num_channels; i++)
        b->channels[i] = ssd_channel_new (ctrl_delay, data_delay, table_size, max_connections);

    return b;
}

Bus *ssd_bus_new(uint num_channels, int64 ctrl_delay, int64 data_delay, uint table_size, uint max_connections)
{
    SSD_NEW(Bus);
    return ssd_bus_init (obj, num_channels, ctrl_delay, data_delay, table_size, max_connections);
}

/* deallocate channels */
void ssd_bus_free(Bus *b)
{
    uint i;
    assert(b->channels != NULL);
    for(i = 0; i < b->num_channels; i++)
        ssd_channel_free (b->channels[i]);
    ssd_free(b->channels);
    ssd_free(b);
}

/* not required before calling lock()
 * but should be used to help ensure correctness
 * controller that talks on all channels should not connect/disconnect
 *  only devices that use a channel should connect/disconnect */
enum status ssd_bus_connect(Bus *b, uint channel)
{
    assert(b->channels != NULL && channel < b->num_channels);
    return ssd_channel_connect(b->channels[channel]);
}

/* not required when finished
 * but should be used to help ensure correctness
 * controller that talks on all channels should not connect/disconnect
 *  only devices that use a channel should connect/disconnect */
enum status ssd_bus_disconnect(Bus *b, uint channel)
{
    assert(b->channels != NULL && channel < b->num_channels);
    return ssd_channel_disconnect(b->channels[channel]);
}

/* lock bus channel for event
 * updates event with bus delay and bus wait time if there is wait time
 * channel will automatically unlock after event is finished using bus
 * assumes event is sent across channel as soon as bus is available
 * event may fail if channel is saturated so check return value
 */
enum status ssd_bus_lock(Bus *b, uint channel, int64 start_time, int64 duration, Event *event)
{
    assert(b->channels != NULL && start_time >= 0 && duration > 0);
    return ssd_channel_lock(b->channels[channel], start_time, duration, event);
}

Channel *ssd_bus_get_channel(Bus *b, uint channel)
{
    assert(b->channels != NULL && channel < b->num_channels);
    return b->channels[channel];
}
