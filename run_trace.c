/* Copyright 2009, 20 Brendan Tauras */

/* run_trace.cpp is part of FlashSim. */

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

/* ASCII trace driver
 * Brendan Tauras 2009-05-21
 *
 * driver to run traces - just provide the ASCII trace file
 * not accurate, for test purposes only
 * goes through trace and treats read requests as writes to prepare the SSD
 *  (requests will fail if there are multiple writes to same address)
 * then goes through the trace again as normal
 *  (write requests may fail if they are attempts to overwrite) */

#include <stdio.h>
#include <stdlib.h>
#include "ssd.h"


int main(int argc, char **argv)
{
    int64 arrive_time;
    uint diskno;
    ulong vaddr;
    uint size;
    uint op;
    char line[80];
    double time;
    int64 read_time = 0;
    int64 write_time = 0;
    int64 read_total = 0;
    int64 write_total = 0;
    unsigned long num_reads = 0;
    unsigned long num_writes = 0;

    load_config();
    print_config(NULL);
    printf("Press ENTER to continue...");
    getchar();
    printf("\n");

    Ssd *ssd = ssd_sim_new(SSD_SIZE);
    printf ("argc=%d, argv[0]=%s\n", argc, argv[0]);
    FILE *trace = NULL;
    if((trace = fopen(argv[1], "r")) == NULL){
        printf("Please provide trace file name\n");
        exit(-1);
    }

    printf("INITIALIZING SSD\n");

#if 0
    /* first go through and write to all read addresses to prepare the SSD */
    while(fgets(line, 80, trace) != NULL){
        sscanf(line, "%lf %u %lu %u %u", &time, &diskno, &vaddr, &size, &op);
        arrive_time = time *1000000000;
        vaddr %= 65536;
        if(op == 1)
            (void) ssd_event_arrive(ssd, WRITE, vaddr, size, arrive_time);
    }
#endif
    printf("STARTING TRACE\n");

    /* now rewind file and run trace */
    fseek(trace, 0, SEEK_SET);
    while(fgets(line, 80, trace) != NULL){
        sscanf(line, "%lf %u %lu %u %u", &time, &diskno, &vaddr, &size, &op);
        arrive_time = time *1000000000;
        vaddr %= 65536;
        if(op == 0){
            write_time = ssd_event_arrive(ssd, SSD_WRITE, vaddr, size, arrive_time);
            if(write_time != 0){
                write_total += write_time;
                num_writes++;
            }
        } else if(op == 1){
            read_time = ssd_event_arrive(ssd, SSD_READ, vaddr, size, arrive_time);
            if(read_time != 0){
                read_total += read_time;
                num_reads++;
            }
        } else
            fprintf(stderr, "Bad operation in trace\n");
    }
    printf("Num reads : %lu\n", num_reads);
    printf("Num writes: %lu\n", num_reads);
    printf("Avg read time : %"FMT_64" ns\n", read_time / num_reads);
    printf("Avg write time: %"FMT_64" ns\n", write_time / num_writes);
    return 0;
}
