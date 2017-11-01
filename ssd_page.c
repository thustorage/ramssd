/* Copyright 2009, 20 Brendan Tauras */

/* ssd_page.cpp is part of FlashSim. */

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

/* Page class
 * Brendan Tauras 2009-04-06
 *
 * The page is the lowest level data storage unit that is the size unit of
 * requests (events).  Pages maintain their state as events modify them. */

#include "ssd.h"


static int64 _read_delay;
static int64 _write_delay;

Page *ssd_page_init (Page *p, const Block *parent, int64 read_delay, int64 write_delay)
{
    p->state = EMPTY;
    p->parent = parent;
    _read_delay = read_delay;
    _write_delay = write_delay;

    if(read_delay < 0){
        SSD_ERROR("Page warning: constructor received negative read delay value\n\tsetting read delay to 0\n");
        _read_delay = 0;
    }

    if(write_delay < 0){
        SSD_ERROR("Page warning: constructor received negative write delay value\n\tsetting write delay to 0\n");
        _write_delay = 0;
    }
    return p;
}

Page *ssd_page_new (const Block *parent, int64 read_delay, int64 write_delay)
{
    SSD_NEW(Page);
    return ssd_page_init (obj, parent, read_delay, write_delay);
}

void ssd_page_free (Page *p)
{
    ssd_free (p);
}
static int g_cnt = 0;
static int g_cnt2 = 0;
#define NOCHECK_PAGE_STATE 0
enum status ssd_page_read(Page *p, Event *event)
{
    assert(_read_delay >= 0);
    if(NOCHECK_PAGE_STATE || p->state == VALID || p->state == EMPTY){
        ssd_event_incr_time_taken(event, _read_delay);
        return SUCCESS;
    } else {
        SSD_ERROR("#%d page =0x%lx, state=%d, %d, ppn=%lu %u\n", g_cnt2, (long)p, p->state, EMPTY, event->logical_address, event->size);
        return FAILURE;
    }
}

enum status ssd_page_write(Page *p, Event *event)
{
    assert(_write_delay >= 0);

    if(NOCHECK_PAGE_STATE || p->state == EMPTY){
        ssd_event_incr_time_taken(event, _write_delay);
        p->state = VALID;
        return SUCCESS;
    } else {
        SSD_ERROR("#%d page =0x%lx, state=%d, %d, ppn=%lu, %u\n", g_cnt, (long)p, p->state, EMPTY, event->logical_address, event->size);
        return FAILURE;
    }
}

const Block *ssd_page_get_parent(Page *p)
{
    return p->parent;
}

enum page_state ssd_page_get_state(Page *p)
{
    return p->state;
}

void ssd_page_set_state(Page *p, enum page_state state)
{
    p->state = state;
}
