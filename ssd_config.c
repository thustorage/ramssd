/* Copyright 2009, 20 Brendan Tauras */

/* ssd_config.cpp is part of FlashSim. */

/* FlashSim is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version. */

/* FlashSim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. */


/****************************************************************************/

/* Configuration loader
 * Brendan Tauras 2009-11-02
 *
 * Functions below provide basic configuration file parsing.  Config file
 * support includes skipping blank lines, comment lines (begin with a #).
 * Parsed lines consist of the variable name, a space, then the value
 * (e.g. SSD_SIZE 4 ).  Default config values (if config file is missing
 * an entry to set the value) are defined in the variable declarations below.
 *
 * A function is also provided for printing the current configuration. */

#ifndef __KERNEL__
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#else
#include <linux/kernel.h>
#endif

/* Define typedefs and error macros from ssd.h here instead of including
 * header file because we want to declare the global configuration variables
 * and set them in this file.  ssd.h declares the global configuration
 * variables as extern const, which would conflict this file's definitions.
 * This is not the best solution, but it is easy and it works. */

/* some obvious typedefs for laziness */

#ifdef __x86_64__
typedef long int64;
#define FMT_64 "ld"
#else
typedef long long int int64;
#define FMT_64 "lld"
#endif

/* define exit codes for errors */
#define MEM_ERR -1
#define FILE_ERR -2


/* Simulator configuration
 * All configuration variables are set by reading ssd.conf and referenced with
 *  as "extern const" in ssd.h
 * Configuration variables are described below and are assigned default values
 *  in case of config file error.  The values defined below are overwritten
 *  when defined in the config file.
 * We do not want a class here because we want to use the configuration
 *  variables in the same was as macros. */

/* Ram class:
 *  delay to read from and write to the RAM for 1 page of data */
int64 RAM_READ_DELAY = 10;
int64 RAM_WRITE_DELAY = 10;

/* Bus class:
 *  delay to communicate over bus
 *  max number of connected devices allowed
 *  number of time entries bus has to keep track of future schedule usage
 *  value used as a flag to indicate channel is free
 *      (use a value not used as a delay value - e.g. -1.0)
 *  number of simultaneous communication channels - defined by SSD_SIZE */
int64 BUS_CTRL_DELAY = 5;
int64 BUS_DATA_DELAY = 10;
unsigned int BUS_MAX_CONNECT = 8;
unsigned int BUS_TABLE_SIZE = 64;
int64 BUS_CHANNEL_FREE_FLAG = (int64)-1;
/* uint BUS_CHANNELS = 4; same as # of Packages, defined by SSD_SIZE */

/* Ssd class:
 *  number of Packages per Ssd (size) */

#include "settings.h"
uint SSD_SIZE = 16;

/* Package class:
 *  number of Dies per Package (size) */
uint PACKAGE_SIZE = 4;

/* Die class:
 *  number of Planes per Die (size) */
uint DIE_SIZE = 2;

/* Plane class:
 *  number of Blocks per Plane (size)
 *  delay for reading from plane register
 *  delay for writing to plane register
 *  delay for merging is based on read, write, reg_read, reg_write
 *      and does not need to be explicitly defined */
#if ((4*SSD_SIZE_X) * 16) > FLASHPGS_PER_BLOCK
uint PLANE_SIZE = (4 * SSD_SIZE_X) * 16/FLASHPGS_PER_BLOCK;
#else
uint PLANE_SIZE = 1;
#endif
int64 PLANE_REG_READ_DELAY = 0;
int64 PLANE_REG_WRITE_DELAY = 0;

/* Block class:
 *  number of Pages per Block (size)
 *  number of erases in lifetime of block
 *  delay for erasing block */
uint SSD_BLOCK_SIZE = FLASHPGS_PER_BLOCK;
uint BLOCK_ERASES = 1048675;
int64 BLOCK_ERASE_DELAY = 150000;

/* Page class:
 *  delay for Page reads
 *  delay for Page writes */
int64 PAGE_READ_DELAY = 5000;
int64 PAGE_WRITE_DELAY = 20000;


#ifndef __KERNEL__
void load_entry(char *name, int64 value, uint line_number)
{
    /* cheap implementation - go through all possibilities and match entry */
    if(!strcmp(name, "RAM_READ_DELAY"))
        RAM_READ_DELAY = value;
    else if(!strcmp(name, "RAM_WRITE_DELAY"))
        RAM_WRITE_DELAY = value;
    else if(!strcmp(name, "BUS_CTRL_DELAY"))
        BUS_CTRL_DELAY = value;
    else if(!strcmp(name, "BUS_DATA_DELAY"))
        BUS_DATA_DELAY = value;
    else if(!strcmp(name, "BUS_MAX_CONNECT"))
        BUS_MAX_CONNECT = (uint) value;
    else if(!strcmp(name, "BUS_TABLE_SIZE"))
        BUS_TABLE_SIZE = (uint) value;
    else if(!strcmp(name, "SSD_SIZE"))
        SSD_SIZE = (uint) value;
    else if(!strcmp(name, "PACKAGE_SIZE"))
        PACKAGE_SIZE = (uint) value;
    else if(!strcmp(name, "DIE_SIZE"))
        DIE_SIZE = (uint) value;
    else if(!strcmp(name, "PLANE_SIZE"))
        PLANE_SIZE = (uint) value;
    else if(!strcmp(name, "PLANE_REG_READ_DELAY"))
        PLANE_REG_READ_DELAY = value;
    else if(!strcmp(name, "PLANE_REG_WRITE_DELAY"))
        PLANE_REG_WRITE_DELAY = value;
    else if(!strcmp(name, "BLOCK_SIZE"))
        SSD_BLOCK_SIZE = (uint) value;
    else if(!strcmp(name, "BLOCK_ERASES"))
        BLOCK_ERASES = (uint) value;
    else if(!strcmp(name, "BLOCK_ERASE_DELAY"))
        BLOCK_ERASE_DELAY = value;
    else if(!strcmp(name, "PAGE_READ_DELAY"))
        PAGE_READ_DELAY = value;
    else if(!strcmp(name, "PAGE_WRITE_DELAY"))
        PAGE_WRITE_DELAY = value;
    else
        fprintf(stderr, "Config file parsing error on line %u\n", line_number);
    return;
}

void load_config(void)
{
    const char * const config_name = "ssd.conf";
    FILE *config_file = NULL;

    /* update sscanf line below with max name length (%s) if changing sizes */
    uint line_size = 128;
    char line[line_size];
    uint line_number;

    char name[line_size];
    int64 value;

    if((config_file = fopen(config_name, "r")) == NULL) {
        printf("Config file %s not found.  Using default value.\n", config_name);
        return;
    }

    for(line_number = 1; fgets(line, line_size, config_file) != NULL; line_number++)
    {
        line[line_size - 1] = '\0';

        /* ignore comments and blank lines */
        if(line[0] == '#' || line[0] == '\n')
            continue;

        /* read lines with entries (name value) */
        if(sscanf(line, "%127s %"FMT_64"", name, &value) == 2)
        {
            name[line_size - 1] = '\0';
            load_entry(name, value, line_number);
        }
        else
            fprintf(stderr, "Config file parsing error on line %u\n", line_number);
    }
    fclose(config_file);
    return;
}

void print_config(FILE *stream)
{
    if(stream == NULL)
        stream = stdout;
    fprintf(stream, "RAM_READ_DELAY: %"FMT_64"\n", RAM_READ_DELAY);
    fprintf(stream, "RAM_WRITE_DELAY: %"FMT_64"\n", RAM_WRITE_DELAY);
    fprintf(stream, "BUS_CTRL_DELAY: %"FMT_64"\n", BUS_CTRL_DELAY);
    fprintf(stream, "BUS_DATA_DELAY: %"FMT_64"\n", BUS_DATA_DELAY);
    fprintf(stream, "BUS_MAX_CONNECT: %u\n", BUS_MAX_CONNECT);
    fprintf(stream, "BUS_TABLE_SIZE: %u\n", BUS_TABLE_SIZE);
    fprintf(stream, "SSD_SIZE: %u\n", SSD_SIZE);
    fprintf(stream, "PACKAGE_SIZE: %u\n", PACKAGE_SIZE);
    fprintf(stream, "DIE_SIZE: %u\n", DIE_SIZE);
    fprintf(stream, "PLANE_SIZE: %u\n", PLANE_SIZE);
    fprintf(stream, "PLANE_REG_READ_DELAY: %"FMT_64"\n", PLANE_REG_READ_DELAY);
    fprintf(stream, "PLANE_REG_WRITE_DELAY: %"FMT_64"\n", PLANE_REG_WRITE_DELAY);
    fprintf(stream, "BLOCK_SIZE: %u\n", SSD_BLOCK_SIZE);
    fprintf(stream, "BLOCK_ERASES: %u\n", BLOCK_ERASES);
    fprintf(stream, "BLOCK_ERASE_DELAY: %"FMT_64"\n", BLOCK_ERASE_DELAY);
    fprintf(stream, "PAGE_READ_DELAY: %"FMT_64"\n", PAGE_READ_DELAY);
    fprintf(stream, "PAGE_WRITE_DELAY: %"FMT_64"\n", PAGE_WRITE_DELAY);
    return;
}
#endif
