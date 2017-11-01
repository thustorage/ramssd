/* Copyright 2009, 20 Brendan Tauras */

/* ssd_plane.cpp is part of FlashSim. */

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

/* Plane class
 * Brendan Tauras 2009-11-03
 *
 * The plane is the data storage hardware unit that contains blocks.
 * Plane-level merges are implemented in the plane.  Planes maintain wear
 * statistics for the FTL. */

#include "ssd.h"

static uint _size = 0;
static int64 _reg_read_delay = 0;
static int64 _reg_write_delay = 0;
static void update_wear_stats(Plane *);
static enum status get_next_page(Plane *);


Plane *ssd_plane_init(Plane *p, const Die *parent, uint plane_size, int64 reg_read_delay, int64 reg_write_delay)
{
    uint i;
    _size = plane_size;
    p->data = (Block **)ssd_malloc (plane_size * sizeof(Block*));
    p->parent = parent;
    p->least_worn = 0;
    p->erases_remaining = BLOCK_ERASES;
    p->last_erase_time = 0;
    p->free_blocks = plane_size;

    if(reg_read_delay < 0) {
        SSD_ERROR("Plane error: %s: constructor received negative register read delay value\n\tsetting register read delay to 0\n", __func__);
        reg_read_delay = 0;
    } else
        _reg_read_delay = reg_read_delay;

    if(reg_write_delay < 0) {
        SSD_ERROR("Plane error: %s: constructor received negative register write delay value\n\tsetting register write delay to 0\n", __func__);
        _reg_write_delay = 0;
    } else
        _reg_write_delay = reg_write_delay;

    /* next page only uses the block, page, and valid fields of the address
     *    object so we can ignore setting the other fields
     * plane does not know about higher-level hardware organization, so we cannot
     *    set the other fields anyway */
    p->next_page.block = 0;
    p->next_page.page = 0;
    p->next_page.valid = PAGE;

    /* new cannot initialize an array with constructor args so
     *  malloc the array
     *  then use placement new to call the constructor for each element
     * chose an array over container class so we don't have to rely on anything
     *  i.e. STL's std::vector */
    /* array allocated in initializer list:
     * data = (Block *) malloc(size * sizeof(Block)); */
    if(p->data == NULL){
        SSD_ERROR("Plane error: %s: constructor unable to allocate Block data\n", __func__);
        ssd_bug(MEM_ERR);
    }

    for(i = 0; i < _size; i++)
        p->data[i] = ssd_block_new (p, SSD_BLOCK_SIZE, BLOCK_ERASES, BLOCK_ERASE_DELAY);

    return p;
}

Plane *ssd_plane_new(const Die *parent, uint plane_size, int64 reg_read_delay, int64 reg_write_delay)
{
    SSD_NEW(Plane);
    return ssd_plane_init (obj, parent, plane_size, reg_read_delay, reg_write_delay);
}

void ssd_plane_free(Plane *p)
{
    uint i;
    assert(p->data != NULL);
    /* call destructor for each Block array element
     * since we used malloc and placement new */
    for(i = 0; i < _size; i++)
        ssd_block_free (p->data[i]);
    ssd_free(p->data);
    ssd_free(p);
}

enum status ssd_plane_read(Plane *p, Event *event)
{
    const Address *a = ssd_event_get_address(event);
    assert((a->block < _size) && a->valid > PLANE);
    return ssd_block_read(p->data[a->block], event);
}

enum status ssd_plane_write(Plane *p, Event *event)
{
    enum block_state prev;
    const Address *a = ssd_event_get_address(event);
    if (!(a->block < _size && a->valid > PLANE && p->next_page.valid >= BLOCK)) {
        //TODO
        SSD_ERROR("addr=%u,%u, %u,  p=%u, PLANE=%u, BLOCK=%u\n", a->block, a->valid, p->next_page.valid, _size, PLANE, BLOCK);
    }
    prev = ssd_block_get_state(p->data[a->block]);
    if(a->block == p->next_page.block)
        /* if all blocks in the plane are full and this function fails,
         * the next_page address valid field will be set to PLANE */
        get_next_page(p);
    if(prev == FREE && ssd_block_get_state(p->data[a->block]) != FREE)
        p->free_blocks--;
    return ssd_block_write(p->data[a->block], event);
}

/* if no errors
 *  updates last_erase_time if later time
 *  updates erases_remaining if smaller value
 * returns 1 for success, 0 for failure */
enum status ssd_plane_erase(Plane *p, Event *event)
{
    enum status status;
    const Address *a = ssd_event_get_address(event);
    assert(a->block < _size && a->valid > PLANE);
    status = ssd_block_erase(p->data[a->block], event);

/* update values if no errors */
    if(status == 1)
    {
        update_wear_stats(p);
        p->free_blocks++;

        /* set next free page if plane was completely full */
        if(p->next_page.valid < PAGE)
            get_next_page(p);
    }
    return status;
}

/* handle everything for a merge operation
 *  address.block and address_merge.block must be valid
 *  move event::address valid pages to event::address_merge empty pages
 * creates own events for resulting read/write operations
 * supports blocks that have different sizes */
enum status ssd_plane_merge(Plane *p, Event *event)
{
    uint i;
    uint merge_count = 0;
    uint merge_avail = 0;
    uint num_merged = 0;
    int64 total_delay = 0;
    uint block_size;
    uint merge_block_size;

    Address read;
    Address write;
    const Address *address;
    const Address *merge_address;
    Event read_event;
    Event write_event;

    /* get and check address validity and size of blocks involved in the merge */
    address = ssd_event_get_address(event);
    merge_address = ssd_event_get_merge_address(event);

    assert(_reg_read_delay >= 0 && _reg_write_delay >= 0);
    assert(address->block < _size && address->valid > PLANE);
    assert(ssd_address_compare(address, merge_address) >= BLOCK);
    assert(address->block < _size && merge_address->block < _size);

    block_size = ssd_block_get_size(p->data[address->block]);
    merge_block_size = ssd_block_get_size(p->data[merge_address->block]);

    /* how many pages must be moved */
    for(i = 0; i < block_size; i++)
        if(ssd_block_get_state2(p->data[address->block], i) == VALID)
            merge_count++;

    /* how many pages are available */
    for(i = 0; i < merge_block_size; i++)
        if(ssd_block_get_state2(p->data[merge_address->block], i) == EMPTY)
            merge_avail++;

/* fail if not enough space to do the merge */
    if(merge_count > merge_avail)
    {
        SSD_ERROR("Plane error: %s: Not enough space to merge block %d into block %d\n", __func__, address->block, merge_address->block);
        return FAILURE;
    }

/* create event classes to handle read and write events for the merge */
    ssd_address_init (&read, address);
    ssd_address_init (&write, merge_address);

    read.page = 0;
    read.valid = PAGE;
    write.page = 0;
    write.valid = PAGE;

    ssd_event_init (&read_event, SSD_READ, 0, 1, ssd_event_get_start_time(event));
    ssd_event_init(&write_event, SSD_WRITE, 0, 1, ssd_event_get_start_time(event));
    ssd_event_set_address(&read_event, &read);
    ssd_event_set_address(&write_event, &write);

/* calculate merge delay and add to event time
 * use i as an error counter */
    for(i = 0; num_merged < merge_count && read.page < block_size; read.page++)
    {
        /* find next page to read from */
        if(ssd_block_get_state2(p->data[read.block], read.page) == VALID)
        {
            /* read from page and set status to invalid */
            if(ssd_block_read(p->data[read.block], &read_event) == 0)
            {
                SSD_ERROR("Plane error: %s: Read for merge block %d into %d failed\n", __func__, read.block, write.block);
                i++;
            }
            ssd_block_invalidate_page(p->data[read.block], read.page);

            /* get time taken for read and plane register write
             * read event time will accumulate and be added at end */
            total_delay += _reg_write_delay;

            /* keep advancing from last page written to */
            for(; write.page < merge_block_size; write.page++)
            {
                /* find next page to write to */
                if(ssd_block_get_state2(p->data[write.block], write.page) == EMPTY)
                {
                    /* write to page (page::_write() sets status to valid) */
                    if(ssd_block_write(p->data[merge_address->block], &write_event) == 0)
                    {
                        SSD_ERROR("Plane error: %s: Write for merge block %d into %d failed\n", __func__, address->block, merge_address->block);
                        i++;
                    }

                    /* get time taken for plane register read
                     * write event time will accumulate and be added at end */
                    total_delay += _reg_read_delay;
                    num_merged++;
                    break;
                }
            }
        }
    }
    total_delay += ssd_event_get_time_taken(&read_event) + ssd_event_get_time_taken(&write_event);
    ssd_event_incr_time_taken(event, total_delay);

/* update next_page for the get_free_page method if we used the page */
    if(p->next_page.valid < PAGE)
        get_next_page(p);

    if(i == 0)
        return SUCCESS;
    else
    {
        SSD_ERROR("Plane error: %s: %u failures during merge operation\n", __func__, i);
        return FAILURE;
    }
}

uint ssd_plane_get_size(Plane *p)
{
    return _size;
}

const Die *ssd_plane_get_parent(Plane *p)
{
    return p->parent;
}

/* if given a valid Block address, call the Block's method
 * else return local value */
int64 ssd_plane_get_last_erase_time(Plane *p, const Address *address)
{
    assert(p->data != NULL);
    if(address->valid > PLANE && address->block < _size)
        return ssd_block_get_last_erase_time(p->data[address->block]);
    else
        return p->last_erase_time;
}

/* if given a valid Block address, call the Block's method
 * else return local value */
ulong ssd_plane_get_erases_remaining(Plane *p, const Address *address)
{
    assert(p->data != NULL);
    if(address->valid > PLANE && address->block < _size)
        return ssd_block_get_erases_remaining(p->data[address->block]);
    else
        return p->erases_remaining;
}

/* Block with the most erases remaining is the least worn */
static void update_wear_stats(Plane *p)
{
    uint i;
    uint max_index = 0;
    ulong max = ssd_block_get_erases_remaining(p->data[0]);
    for(i = 1; i < _size; i++)
        if(ssd_block_get_erases_remaining(p->data[i]) > max)
            max_index = i;
    p->least_worn = max_index;
    p->erases_remaining = max;
    p->last_erase_time = ssd_block_get_last_erase_time(p->data[max_index]);
    return;
}

/* update given address->block to least worn block */
void ssd_plane_get_least_worn(Plane *p, Address *address)
{
    assert(p->least_worn < _size);
    address->block = p->least_worn;
    address->valid = BLOCK;
    return;
}

enum page_state ssd_plane_get_state(Plane *p, const Address *address)
{
    assert(p->data != NULL && address->block < _size && address->valid >= PLANE);
    return ssd_block_get_state3(p->data[address->block], address);
}

/* update address to next free page in plane
 * error condition will result in (address->valid < PAGE) */
void ssd_plane_get_free_page(Plane *p, Address *address)
{
    address->block = p->next_page.block;
    address->page = p->next_page.page;
    address->valid = p->next_page.valid;
}

/* internal method to keep track of the next usable (free or active) page in
 *    this plane
 * method is called by write and erase methods and calls Block::ssd_plane_get_next_page(p)
 *    such that the get_free_page method can run in constant time */
static enum status get_next_page(Plane *p)
{
    uint i;
    p->next_page.valid = PLANE;

    for(i = 0; i < _size; i++)
    {
        if(ssd_block_get_state(p->data[i]) != INACTIVE)
        {
            p->next_page.valid = BLOCK;
            if(ssd_block_get_next_page(p->data[i], &p->next_page) == SUCCESS)
            {
                p->next_page.block = i;
                return SUCCESS;
            }
        }
    }
    return FAILURE;
}

/* free_blocks is updated in the write and erase methods */
uint ssd_plane_get_num_free(Plane *p, const Address *address)
{
    assert(address->valid >= PLANE);
    return p->free_blocks;
}

uint ssd_plane_get_num_valid(Plane *p, const Address *address)
{
    assert(address->valid >= PLANE);
    return ssd_block_get_pages_valid(p->data[address->block]);
}
