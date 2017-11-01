/* Copyright 2009, 20 Brendan Tauras */

/* ssd_die.cpp is part of FlashSim. */

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

/* Die class
 * Brendan Tauras 2009-11-03
 *
 * The die is the data storage hardware unit that contains planes and is a flash
 * chip.  Dies maintain wear statistics for the FTL. */

#include "ssd.h"

static void update_wear_stats(Die *d, const Address *address);

Die *ssd_die_init(Die *d, const Package *parent, Channel *channel, uint die_size)
{
    uint i;

    d->size = die_size;
    d->data = (Plane **) ssd_malloc(d->size * sizeof(Plane*));
    d->parent = parent;
    d->channel = channel;
    d->least_worn = 0;
    d->erases_remaining = BLOCK_ERASES;
    d->last_erase_time = 0;

    if(ssd_channel_connect(d->channel) == FAILURE)
        SSD_ERROR("Die error: %s: constructor unable to connect to Bus Channel\n", __func__);

    /* new cannot initialize an array with constructor args so
     *  malloc the array
     *  then use placement new to call the constructor for each element
     * chose an array over container class so we don't have to rely on anything
     *  i.e. STL's std::vector */
    /* array allocated in initializer list:
     * data = (Plane *) malloc(size * sizeof(Plane)); */
    if(d->data == NULL){
        SSD_ERROR("Die error: %s: constructor unable to allocate Plane data\n", __func__);
        ssd_bug(MEM_ERR);
    }
    for(i = 0; i < d->size; i++)
        d->data[i] = ssd_plane_new (d, PLANE_SIZE, PLANE_REG_READ_DELAY, PLANE_REG_WRITE_DELAY);

    return d;
}

Die *ssd_die_new(const Package *parent, Channel *channel, uint die_size)
{
    SSD_NEW(Die);
    return ssd_die_init(obj, parent, channel, die_size);
}

void ssd_die_free(Die *d)
{
    uint i;
    assert(d->data != NULL);
    /* call destructor for each Block array element
     * since we used malloc and placement new */
    for(i = 0; i < d->size; i++)
        ssd_plane_free (d->data[i]);
    ssd_free(d->data);
    ssd_channel_disconnect(d->channel);
    ssd_free (d);
}

enum status ssd_die_read(Die *d, Event *event)
{
    assert(d->data != NULL);
    assert(ssd_event_get_address(event)->plane < d->size && ssd_event_get_address(event)->valid > DIE);
    return ssd_plane_read(d->data[ssd_event_get_address(event)->plane], event);
}

enum status ssd_die_write(Die *d, Event *event)
{
    assert(d->data != NULL);
    assert(ssd_event_get_address(event)->plane < d->size && ssd_event_get_address(event)->valid > DIE);
    return ssd_plane_write(d->data[ssd_event_get_address(event)->plane], event);
}

/* if no errors
 *  updates last_erase_time if later time
 *  updates erases_remaining if smaller value
 * returns 1 for success, 0 for failure */
enum status ssd_die_erase(Die *d, Event *event)
{
    enum status status;
    assert(d->data != NULL);
    assert(ssd_event_get_address(event)->plane < d->size && ssd_event_get_address(event)->valid > DIE);
    status = ssd_plane_erase(d->data[ssd_event_get_address(event)->plane], event);

    /* update values if no errors */
    if(status == SUCCESS)
        update_wear_stats(d, ssd_event_get_address(event));
    return status;
}

/* TODO: move Plane::_merge() to Die and make generic to handle merge across
 *  both cases: 2 separate planes or within 1 plane */
enum status ssd_die_merge(Die *d, Event *event)
{
    assert(d->data != NULL);
    assert(ssd_event_get_address(event)->plane < d->size && ssd_event_get_address(event)->valid > DIE && ssd_event_get_merge_address(event)->plane < d->size && ssd_event_get_merge_address(event)->valid > DIE);
    if(ssd_event_get_address(event)->plane != ssd_event_get_merge_address(event)->plane)
        return ssd_die__merge(d, event);
    else return ssd_plane_merge(d->data[ssd_event_get_address(event)->plane], event);
}

/* TODO: update stub as per ssd_die_merge() comment above
 * to support Die-level merge operations */
enum status ssd_die__merge(Die *d, Event *event)
{
    const Address *a = ssd_event_get_address(event);
    assert(d->data != NULL);
    assert(a->plane < d->size && a->valid > DIE && ssd_event_get_merge_address(event)->plane < d->size && ssd_event_get_merge_address(event)->valid > DIE);
    assert(a->plane != ssd_event_get_merge_address(event)->plane);
    return SUCCESS;
}

const Package *ssd_die_get_parent(Die *d)
{
    return d->parent;
}

/* if given a valid Block address, call the Block's method
 * else return local value */
int64 ssd_die_get_last_erase_time(Die *d, const Address *address)
{
    assert(d->data != NULL);
    if(address->valid > DIE && address->plane < d->size)
        return ssd_plane_get_last_erase_time(d->data[address->plane], address);
    else
        return d->last_erase_time;
}

/* if given a valid Plane address, call the Plane's method
 * else return local value */
ulong ssd_die_get_erases_remaining(Die *d, const Address *address)
{
    assert(d->data != NULL);
    if(address->valid > DIE && address->plane < d->size)
        return ssd_plane_get_erases_remaining(d->data[address->plane],address);
    else
        return d->erases_remaining;
}

/* Plane with the most erases remaining is the least worn */
static void update_wear_stats(Die *d, const Address *address)
{
    uint i;
    uint max_index = 0;
    ulong max;
    assert(d->data != NULL);
    max = ssd_plane_get_erases_remaining(d->data[0], address);
    for(i = 1; i < d->size; i++)
        if(ssd_plane_get_erases_remaining(d->data[i], address) > max)
            max_index = i;
    d->least_worn = max_index;
    d->erases_remaining = max;
    d->last_erase_time = ssd_plane_get_last_erase_time(d->data[max_index], address);
}

/* update given address->die to least worn die */
void ssd_die_get_least_worn(Die *d, Address *address)
{
    assert(d->data != NULL && d->least_worn < d->size);
    address->plane = d->least_worn;
    address->valid = PLANE;
    ssd_plane_get_least_worn(d->data[d->least_worn], address);
}

enum page_state ssd_die_get_state(Die *d, const Address *address)
{
    assert(d->data != NULL && address->plane < d->size && address->valid >= DIE);
    return ssd_plane_get_state(d->data[address->plane], address);
}

void ssd_die_get_free_page(Die *d, Address *address)
{
    assert(address->plane < d->size && address->valid >= PLANE);
    ssd_plane_get_free_page(d->data[address->plane], address);
}

uint ssd_die_get_num_free(Die *d, const Address *address)
{
    assert(address->valid >= PLANE);
    return ssd_plane_get_num_free(d->data[address->plane], address);
}

uint ssd_die_get_num_valid(Die *d, const Address *address)
{
    assert(address->valid >= PLANE);
    return ssd_plane_get_num_valid(d->data[address->plane], address);
}
