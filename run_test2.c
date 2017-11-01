/* Copyright 2009, 20 Brendan Tauras */

/* run_test2.cpp is part of FlashSim. */

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

/* Basic test driver
 * Brendan Tauras 20-08-03
 *
 * driver to create and run a very basic test of writes then reads */

#include "ssd.h"
#include <stdio.h>

#define SIZE 10


int main()
{
    int i;
    load_config();
    print_config(NULL);
    printf("Press ENTER to continue...");
    getchar();
    printf("\n");

    Ssd *ssd = ssd_sim_new(SSD_SIZE);

    int64 result;
    int64 cur_time = 1;
    int64 delta = BUS_DATA_DELAY - 2 > 0 ? BUS_DATA_DELAY - 2 : BUS_DATA_DELAY;

    for (i = 0; i < SIZE; i++, cur_time += delta) {
        /* event_arrive(event_type, logical_address, size, start_time) */
        result = ssd_event_arrive(ssd, SSD_WRITE, i, 1, cur_time);
        result = ssd_event_arrive(ssd, SSD_WRITE, i+10240, 1, cur_time);
    }
    for (i = 0; i < SIZE; i++, cur_time += delta) {
        /* event_arrive(event_type, logical_address, size, start_time) */
        result = ssd_event_arrive(ssd, SSD_READ, 1, 1, cur_time);
        result = ssd_event_arrive(ssd, SSD_READ, i, 1, cur_time);
    }
    ssd_sim_free(ssd);
    return 0;
}
