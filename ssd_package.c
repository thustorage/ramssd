/* Copyright 2009, 20 Brendan Tauras */

/* ssd_package.cpp is part of FlashSim. */

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

/* Package class
 * Brendan Tauras 2009-11-03
 *
 * The package is the highest level data storage hardware unit.  While the
 * package is a virtual component, events are passed through the package for
 * organizational reasons, including helping to simplify maintaining wear
 * statistics for the FTL. */


#include "ssd.h"


static void update_wear_stats (Package *p, const Address *address);

Package *ssd_package_init(Package *p, const Ssd *parent, Channel *channel, uint package_size)
{
    uint i;

    p->size = package_size;

    /* use a const pointer (Die * const data) to use as an array
     * but like a reference, we cannot reseat the pointer */
    p->data =  (Die **) ssd_malloc(package_size * sizeof(Die*));
    p->parent = parent;

    /* assume all Dies are same so first one can start as least worn */
    p->least_worn = 0;

    /* set erases remaining to BLOCK_ERASES to match Block constructor args
     * in Plane class
     * this is the cheap implementation but can change to pass through classes */
    p->erases_remaining = BLOCK_ERASES;

    /* assume hardware created at time 0 and had an implied free erasure */
    p->last_erase_time = 0;

    /* new cannot initialize an array with constructor args so
     *  malloc the array
     *  then use placement new to call the constructor for each element
     * chose an array over container class so we don't have to rely on anything
     *  i.e. STL's std::vector */
    /* array allocated in initializer list:
     *  data = (Die *) malloc(size * sizeof(Die)); */
    if(p->data == NULL){
        SSD_ERROR("Package error: %s: constructor unable to allocate Die data\n", __func__);
        ssd_bug(MEM_ERR);
    }

    for(i = 0; i < p->size; i++)
        p->data[i] = ssd_die_new(p, channel, DIE_SIZE);
    return p;
}

Package *ssd_package_new(const Ssd *parent, Channel *channel, uint package_size)
{
    SSD_NEW(Package);
    return ssd_package_init (obj, parent, channel, package_size);
}

void ssd_package_free(Package *p)
{
    uint i;
    assert(p->data != NULL);
    /* call destructor for each Block array element
     * since we used malloc and placement new */
    for(i = 0; i < p->size; i++)
        ssd_die_free(p->data[i]);
    ssd_free(p->data);
    ssd_free (p);
}

enum status ssd_package_read(Package *p, Event *event)
{
    assert(p->data != NULL && ssd_event_get_address(event)->die < p->size && ssd_event_get_address(event)->valid > PACKAGE);
    return ssd_die_read(p->data[ssd_event_get_address(event)->die], event);
}

enum status ssd_package_write(Package *p, Event *event)
{
    assert(p->data != NULL && ssd_event_get_address(event)->die < p->size && ssd_event_get_address(event)->valid > PACKAGE);
    return ssd_die_write(p->data[ssd_event_get_address(event)->die], event);
}

enum status ssd_package_erase(Package *p, Event *event)
{
    enum status status;
    assert(p->data != NULL && ssd_event_get_address(event)->die < p->size && ssd_event_get_address(event)->valid > PACKAGE);
    status = ssd_die_erase(p->data[ssd_event_get_address(event)->die], event);
    if(status == SUCCESS)
        update_wear_stats(p, ssd_event_get_address(event));
    return status;
}

enum status ssd_package_merge(Package *p, Event *event)
{
    assert(p->data != NULL && ssd_event_get_address(event)->die < p->size && ssd_event_get_address(event)->valid > PACKAGE);
    return ssd_die_merge(p->data[ssd_event_get_address(event)->die], event);
}

const Ssd *ssd_package_get_parent(Package *p)
{
    return p->parent;
}

/* if given a valid Block address, call the Block's method
 * else return local value */
int64 ssd_package_get_last_erase_time(Package *p, const Address *address)
{
    assert(p->data != NULL);
    if(address->valid > PACKAGE && address->die < p->size)
        return ssd_die_get_last_erase_time(p->data[address->die], address);
    else
        return p->last_erase_time;
}

/* if given a valid Die address, call the Die's method
 * else return local value */
ulong ssd_package_get_erases_remaining(Package *p, const Address *address)
{
    assert(p->data != NULL);
    if(address->valid > PACKAGE && address->die < p->size)
        return ssd_die_get_erases_remaining(p->data[address->die], address);
    else
        return p->erases_remaining;
}

/* Plane with the most erases remaining is the least worn */
static void update_wear_stats(Package *p, const Address *address)
{
    uint i;
    uint max_index = 0;
    ulong max = ssd_die_get_erases_remaining(p->data[0], address);
    for(i = 1; i < p->size; i++)
        if(ssd_die_get_erases_remaining(p->data[i], address) > max)
            max_index = i;
    p->least_worn = max_index;
    p->erases_remaining = max;
    p->last_erase_time = ssd_die_get_last_erase_time(p->data[max_index], address);
    return;
}

/* update given address->package to least worn package */
void ssd_package_get_least_worn(Package *p, Address *address)
{
    assert(p->least_worn < p->size);
    address->die = p->least_worn;
    address->valid = DIE;
    ssd_die_get_least_worn(p->data[p->least_worn], address);
    return;
}

enum page_state ssd_package_get_state(Package *p, const Address *address)
{
    assert(p->data != NULL && address->die < p->size && address->valid >= PACKAGE);
    return ssd_die_get_state(p->data[address->die], address);
}

void ssd_package_get_free_page(Package *p, Address *address)
{
    assert(address->die < p->size && address->valid >= DIE);
    ssd_die_get_free_page(p->data[address->die], address);

}
uint ssd_package_get_num_free(Package *p, const Address *address)
{
    assert(address->valid >= DIE);
    return ssd_die_get_num_free(p->data[address->die], address);
}

uint ssd_package_get_num_valid(Package *p, const Address *address)
{
    assert(address->valid >= DIE);
    return ssd_die_get_num_valid(p->data[address->die], address);
}
