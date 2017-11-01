/* Copyright 2009, 20 Brendan Tauras */

/* ssd_block.cpp is part of FlashSim. */

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

/* Block class
 * Brendan Tauras 2009-10-26
 *
 * The block is the data storage hardware unit where erases are implemented.
 * Blocks maintain wear statistics for the FTL. */

#include "ssd.h"

static uint _size = 0;
static int64 _erase_delay;

Block *ssd_block_init (Block *b, const Plane *parent, uint block_size, ulong erases_remaining, int64 erase_delay)
{
    uint i;
    _size = block_size;
    b->data = (Page **) ssd_malloc(block_size * sizeof(Page*));
    b->parent = parent;
    b->pages_valid = 0;
    b->pages_invalid = 0;
    b->state = FREE;
#ifdef HAS_WL
    b->erases_remaining = erases_remaining;
#endif
    b->last_erase_time = 0;
    _erase_delay = erase_delay;

    if(_erase_delay < 0)
    {
        SSD_ERROR("Block warning: %s: constructor received negative erase delay value\n\tsetting erase delay to 0\n", __func__);
        _erase_delay = 0;
    }

    /* new cannot initialize an array with constructor args so
     *  malloc the array
     *  then use placement new to call the constructor for each element
     * chose an array over container class so we don't have to rely on anything
     *  i.e. STL's std::vector */
    /* array allocated in initializer list:
     * data = (Page *) malloc(size * sizeof(Page)); */
    if(b->data == NULL){
        SSD_ERROR("Block error: %s: constructor unable to allocate Page data\n", __func__);
        ssd_bug(MEM_ERR);
    }
    for(i = 0; i < _size; i++) {
        b->data[i] = ssd_page_new (b, PAGE_READ_DELAY, PAGE_WRITE_DELAY);
    }
    return b;
}

Block *ssd_block_new (const Plane *parent, uint block_size, ulong erases_remaining, int64 erase_delay)
{
    SSD_NEW(Block);
    return ssd_block_init(obj, parent, block_size, erases_remaining, erase_delay);
}

void ssd_block_free(Block *b)
{
    uint i;
    assert(b->data != NULL);

    /* call destructor for each Page array element
     * since we used malloc and placement new */
    for(i = 0; i < _size; i++)
        ssd_page_free (b->data[i]);
    ssd_free(b->data);
    ssd_free (b);
}

enum status ssd_block_read(Block *b, Event *event)
{
    assert(b->data != NULL);
    return ssd_page_read(b->data[ssd_event_get_address(event)->page], event);
}

enum status ssd_block_write(Block *b, Event *event)
{
    enum status ret;
    assert(b->data != NULL);
    ret = ssd_page_write(b->data[ssd_event_get_address(event)->page],event);
    if(ret == SUCCESS) {
        b->pages_valid++;
        b->state = ACTIVE;
    } else {
        SSD_ERROR("block pages #%d valid=%d, size=%d\n", ssd_event_get_address(event)->page, b->pages_valid, _size);
    }
    return ret;
}

/* updates Event time_taken
 * sets Page statuses to EMPTY
 * updates last_erase_time and erases_remaining
 * returns 1 for success, 0 for failure */
enum status ssd_block_erase(Block *b, Event *event)
{
    uint i;
    assert(b->data != NULL && _erase_delay >= 0);
#if  0
    if(b->erases_remaining < 1) {
        SSD_ERROR("Block error: %s: No erases remaining when attempting to erase\n", __func__);
        return FAILURE;
    }
    b->erases_remaining--;
#endif
    for(i = 0; i < _size; i++)
        ssd_page_set_state(b->data[i], EMPTY);
    ssd_event_incr_time_taken(event, _erase_delay);
    b->last_erase_time = ssd_event_get_start_time(event) + ssd_event_get_time_taken(event);
    b->pages_valid = 0;
    b->pages_invalid = 0;
    b->state = FREE;
    return SUCCESS;
}

const Plane *ssd_block_get_parent(Block *b)
{
    return b->parent;
}

uint ssd_block_get_pages_valid(Block *b)
{
    return b->pages_valid;
}

uint ssd_block_get_pages_invalid(Block *b)
{
    return b->pages_invalid;
}


enum block_state ssd_block_get_state(Block *b)
{
    return b->state;
}

enum page_state ssd_block_get_state2(Block *b, uint page)
{
    assert(b->data != NULL && page < _size);
    return ssd_page_get_state(b->data[page]);
}

enum page_state ssd_block_get_state3(Block *b, const Address *address)
{
    assert(b->data != NULL && address->page < _size && address->valid >= BLOCK);
    return ssd_page_get_state(b->data[address->page]);
}

int64 ssd_block_get_last_erase_time(Block *b)
{
    return b->last_erase_time;
}

ulong ssd_block_get_erases_remaining(Block *b)
{
#ifdef HAS_WL
    return b->erases_remaining;
#else
    return 1000;
#endif
}

uint ssd_block_get_size(Block *b)
{
    return _size;
}

void ssd_block_invalidate_page(Block *b, uint page)
{
    assert(page < _size);
    ssd_page_set_state(b->data[page], INVALID);
    b->pages_invalid++;

    /* update block state */
    if(b->pages_invalid >= _size)
        b->state = INACTIVE;
    else if(b->pages_valid > 0 || b->pages_invalid > 0)
        b->state = ACTIVE;
    else
        b->state = FREE;

    return;
}

/* method to find the next usable (empty) page in this block
 * method is called by write and erase methods and in Plane::get_next_page() */
enum status ssd_block_get_next_page(Block *b, Address *address)
{
    uint i;

    for(i = 0; i < _size; i++)
    {
        if(ssd_page_get_state(b->data[i]) == EMPTY)
        {
            address->page = i;
            address->valid = PAGE;
            return SUCCESS;
        }
    }
    return FAILURE;
}
