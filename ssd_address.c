/* Copyright 2009, 20 Brendan Tauras */

/* ssd_address.cpp is part of FlashSim. */

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

/* Address class
 * Brendan Tauras 2009-06-19
 *
 * Class to manage physical addresses for the SSD.  It was designed to have
 * public members like a struct for quick access but also have checking,
 * printing, and assignment functionality.  An instance is created for each
 * physical address in the Event class.
 */

#include "ssd.h"

#if 0
Address *ssd_address_new (uint package, uint die, uint plane, uint block, uint page, enum address_valid valid)
{
    SSD_NEW(Address);

    obj->package = package;
    obj->die = die;
    obj->plane = plane;
    obj->block = block;
    obj->page = page;
    obj->valid = valid;
    return obj;
}

void ssd_address_free (Address *a)
{
    ssd_free (a);
}
#endif

Address *ssd_address_init (Address *a, const Address *address)
{
    *a = *address;
    return a;
}

/* default values for parameters are the global settings
 * see "enum address_valid" in ssd.h for details on valid status
 * note that method only checks for out-of-bounds types of errors */
enum address_valid ssd_address_check_valid(Address *a, uint ssd_size, uint package_size, uint die_size, uint plane_size, uint block_size)
{
    enum address_valid tmp = NONE;

    /* must check current valid status first
     * so we cannot expand the valid status */
    if(a->valid >= PACKAGE && a->package < ssd_size) {
        tmp = PACKAGE;
        if(a->valid >= DIE && a->die < package_size) {
            tmp = DIE;
            if(a->valid >= PLANE && a->plane < die_size) {
                tmp = PLANE;
                if(a->valid >= BLOCK && a->block < plane_size) {
                    tmp = BLOCK;
                    if(a->valid >= PAGE && a->page < block_size)
                        tmp = PAGE;
                }
            }
        }
    }
    else
        tmp = NONE;
    a->valid = tmp;
    return a->valid;
}

/* returns enum indicating to what level two addresses match
 * limits comparison to the fields that are valid */
enum address_valid ssd_address_compare(const Address *a, const Address *address)
{
    enum address_valid match = NONE;
    if(a->package == address->package && a->valid >= PACKAGE && address->valid >= PACKAGE) {
        match = PACKAGE;
        if(a->die == address->die && a->valid >= DIE && address->valid >= DIE) {
            match = DIE;
            if(a->plane == address->plane && a->valid >= PLANE && address->valid >= PLANE) {
                match = PLANE;
                if(a->block == address->block && a->valid >= BLOCK && address->valid >= BLOCK) {
                    match = BLOCK;
                    if(a->page == address->page && a->valid >= PAGE && address->valid >= PAGE) {
                        match = PAGE;
                    }
                }
            }
        }
    }
    return match;
}

/* default stream is stdout */
void ssd_address_print(const Address *a)
{
    SSD_DEBUG("(%d, %d, %d, %d, %d, %d)", a->package, a->die, a->plane, a->block, a->page, (int) a->valid);
    return;
}
