

/* 
 * Revised by Storage Research Group at Tsinghua University (thustorage)
 * http://storage.cs.tsinghua.edu.cn
 * 
 * (C) 2012 thustorage
 * Emulate Open-Channle SSD in the memory
 * part of the "Software Manged Flash" Project: http://storage.cs.tsinghua.edu.cn/~lu/research/smf.html
 *
 */

/****************************************************************************/

/* Copyright 2009, 2010 Brendan Tauras */

/* ssd.h is part of FlashSim. */

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

/* ssd.h
 * Brendan Tauras 20-07-16
 * Main SSD header file
 *  Lists definitions of all classes, structures,
 *      typedefs, and constants used in ssd namespace
 *      Controls options, such as debug asserts and test code insertions
 */



#ifndef _SSD_H
#define _SSD_H

#ifndef __x86_64__
typedef long long int int64;
#define FMT_64 "lld"
#else
typedef long int64;
#define FMT_64 "ld"

#endif


#ifndef __KERNEL__
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

typedef unsigned int uint;
typedef unsigned long ulong;

/* Simulator configuration from ssd_config.cpp */
/* Configuration file parsing for extern config variables defined below */

void load_entry(char *name, int64 value, unsigned int line_number);
void load_config(void);
void print_config(FILE *stream);

#define ssd_bug(x) exit(x)

#define SSD_DEBUG(fmt, args...) printf("ssd(%s,%d): " fmt, __func__, __LINE__, ##args)
#define SSD_ERROR(fmt, args...) fprintf(stderr, "ssd(%s,%d): " fmt, __func__, __LINE__, ##args)

#else
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/bug.h>
#include <linux/vmalloc.h>

#define assert(x) BUG_ON(!(x))
#define ssd_bug(x) BUG()

#ifdef _DEBUG
#define SSD_DEBUG(fmt, args...) printk( KERN_INFO "ssd(%s,%d): " fmt, __func__, __LINE__, ##args)
#else
#define SSD_DEBUG(fmt, args...) do{}while(0)
#endif
#define SSD_ERROR(fmt, args...) printk( KERN_ERR "ssd(%s,%d): " fmt, __func__, __LINE__, ##args)
#define warning(fmt, args...) printk( KERN_INFO "[FLASHSIM](#%d,%s): " fmt, __LINE__, __func__, ##args)
#endif

#include "ssdSim.h"

void *ssd_malloc(int x);
void ssd_free(void *p);


/* define exit codes for errors */
#define MEM_ERR -1
#define FILE_ERR -2

/* Uncomment to disable asserts for production */
#define NDEBUG




/* Ram class:
 *  delay to read from and write to the RAM for 1 page of data */
extern const int64 RAM_READ_DELAY;
extern const int64 RAM_WRITE_DELAY;

/* Bus class:
 *  delay to communicate over bus
 *  max number of connected devices allowed
 *  flag value to detect free table entry (keep this negative)
 *  number of time entries bus has to keep track of future schedule usage
 *  number of simultaneous communication channels - defined by SSD_SIZE */
extern const int64 BUS_CTRL_DELAY;
extern const int64 BUS_DATA_DELAY;
extern const uint BUS_MAX_CONNECT;
extern const int64 BUS_CHANNEL_FREE_FLAG;
extern const uint BUS_TABLE_SIZE;
/* extern const uint BUS_CHANNELS = 4; same as # of Packages, defined by SSD_SIZE */

/* Ssd class:
 *  number of Packages per Ssd (size) */
extern const uint SSD_SIZE;

/* Package class:
 *  number of Dies per Package (size) */
extern const uint PACKAGE_SIZE;

/* Die class:
 *  number of Planes per Die (size) */
extern const uint DIE_SIZE;

/* Plane class:
 *  number of Blocks per Plane (size)
 *  delay for reading from plane register
 *  delay for writing to plane register
 *  delay for merging is based on read, write, reg_read, reg_write
 *      and does not need to be explicitly defined */
extern const uint PLANE_SIZE;
extern const int64 PLANE_REG_READ_DELAY;
extern const int64 PLANE_REG_WRITE_DELAY;

/* Block class:
 *  number of Pages per Block (size)
 *  number of erases in lifetime of block
 *  delay for erasing block */
extern const uint SSD_BLOCK_SIZE;
extern const uint BLOCK_ERASES;
extern const int64 BLOCK_ERASE_DELAY;

/* Page class:
 *  delay for Page reads
 *  delay for Page writes */
extern const int64 PAGE_READ_DELAY;
extern const int64 PAGE_WRITE_DELAY;


/* Enumerations to clarify status integers in simulation
 * Do not use typedefs on enums for reader clarity */

/* Page states
 *  empty   - page ready for writing (and contains no valid data)
 *  valid   - page has been written to and contains valid data
 *  invalid - page has been written to and does not contain valid data */
enum page_state{EMPTY, VALID, INVALID};

/* Block states
 *  free     - all pages in block are empty
 *  active   - some pages in block are valid, others are empty or invalid
 *  inactive - all pages in block are invalid */
enum block_state{FREE, ACTIVE, INACTIVE};

/* I/O request event types
 *  read  - read data from address
 *  write - write data to address (page state set to valid)
 *  erase - erase block at address (all pages in block are erased -
 *                                  page states set to empty)
 *  merge - move valid pages from block at address (page state set to invalid)
 *             to free pages in block at merge_address */
//enum event_type{READ, WRITE, ERASE, MERGE};

/* General return status
 * return status for simulator operations that only need to provide general
 * failure notifications */
enum status{FAILURE, SUCCESS};

/* Address valid status
 * used for the valid field in the address class
 * example: if valid == BLOCK, then
 *  the package, die, plane, and block fields are valid
 *  the page field is not valid */
enum address_valid{NONE, PACKAGE, DIE, PLANE, BLOCK, PAGE};


#define SSD_NEW(_T)                             \
    _T *obj = (_T *) ssd_malloc (sizeof(_T));   \
    if (!obj)                                   \
        ssd_bug(1);



/* List classes up front for classes that have references to their "parent"
 * (e.g. a Package's parent is a Ssd).
 *
 * The order of definition below follows the order of this list to support
 * cases of agregation where the agregate class should be defined first.
 * Defining the agregate class first enables use of its non-default
 * constructors that accept args
 * (e.g. a Ssd contains a Controller, Ram, Bus, and Packages). */

struct Address;
struct Event;
struct Channel;
struct Bus;
struct Page;
struct Block;
struct Plane;
struct Die;
struct Package;
struct Garbage_Collector;
struct Wear_Leveler;
struct Ftl;
struct Ram;
struct Controller;
struct Ssd;

/* Class to manage physical addresses for the SSD.  It was designed to have
 * public members like a struct for quick access but also have checking,
 * printing, and assignment functionality.  An instance is created for each
 * physical address in the Event class. */
//class Address
typedef struct Address {
    uint package;
    uint die;
    uint plane;
    uint block;
    uint page;
    enum address_valid valid;
}Address;

Address *ssd_address_init (Address *a, const Address *address);
enum address_valid ssd_address_check_valid(Address *a, uint ssd_size, uint package_size, uint die_size, uint plane_size, uint block_size);
enum address_valid ssd_address_compare(const Address *a, const Address *address);
void ssd_address_print(const Address *a);


/* Class to manage I/O requests as events for the SSD.  It was designed to keep
 * track of an I/O request by storing its type, addressing, and timing.  The
 * SSD class creates an instance for each I/O request it receives. */
typedef struct Event {
    int64 start_time;
    int64 time_taken;
    int64 bus_wait_time;
    enum event_type type;
    ulong logical_address;
    Address address;
    Address merge_address;
    uint size;
    struct Event *next;
} Event;

Event *ssd_event_init (Event *e, enum event_type type, ulong logical_address, uint size, int64 start_time);
const Address *ssd_event_get_address(Event *e);
const Address *ssd_event_get_merge_address(Event *e);
enum event_type ssd_event_get_event_type(Event *e);
int64 ssd_event_get_start_time(Event *e);
int64 ssd_event_get_time_taken(Event *e);
int64 ssd_event_get_bus_wait_time(Event *e);
Event *ssd_event_get_next(Event *e);
void ssd_event_set_address(Event *e, const Address *address);
int64 ssd_event_incr_bus_wait_time(Event *e, int64 time_incr);
int64 ssd_event_incr_time_taken(Event *e, int64 time_incr);
void ssd_event_print(Event *e);


/* Quicksort for Channel class
 * Supply base pointer to array to be sorted along with inclusive range of
 * indices to sort.  The move operations for sorting the first array will also
 * be performed on the second array, or the second array can be NULL.  The
 * second array is useful for the channel scheduling table where we want to
 * sort by one row and keep data pairs in columns together. */
/* extern "C" void quicksort(int64 *array1, int64 *array2, long left, long right); */
void quicksort(int64 *array1, int64 *array2, long left, long right);

/* internal quicksort functions listed for documentation purposes
 * long partition(int64 *array1, int64 *array2, long left, long right);
 * void swap(int64 *x, int64 *y); */

/* Single bus channel
 * Simulate multiple devices on 1 bus channel with variable bus transmission
 * durations for data and control delays with the Channel class.  Provide the
 * delay times to send a control signal or 1 page of data across the bus
 * channel, the bus table size for the maximum number channel transmissions that
 * can be queued, and the maximum number of devices that can connect to the bus.
 * To elaborate, the table size is the size of the channel scheduling table that
 * holds start and finish times of events that have not yet completed in order
 * to determine where the next event can be scheduled for bus utilization. */
typedef struct Channel {
    uint table_size;
    int64 * lock_time;
    int64 * unlock_time;
    uint table_entries;
    uint selected_entry;
    uint num_connected;
    uint max_connections;
    int64 ctrl_delay;
    int64 data_delay;
} Channel;

Channel *ssd_channel_new(int64 ctrl_delay, int64 data_delay, uint table_size, uint max_connections);
void ssd_channel_free(Channel *c);
enum status ssd_channel_connect(Channel *c);
enum status ssd_channel_disconnect(Channel *c);
enum status ssd_channel_lock(Channel *c, int64 start_time, int64 duration, Event *event);
void ssd_channel_unlock(Channel *c, int64 start_time);


/* Multi-channel bus comprised of Channel class objects
 * Simulates control and data delays by allowing variable channel lock
 * durations.  The sender (controller class) should specify the delay (control,
 * data, or both) for events (i.e. read = ctrl, ctrl+data; write = ctrl+data;
 * erase or merge = ctrl).  The hardware enable signals are implicitly
 * simulated by the sender locking the appropriate bus channel through the lock
 * method, then sending to multiple devices by calling the appropriate method
 * in the Package class. */
typedef struct Bus {
    uint num_channels;
    Channel **channels;
} Bus;

Bus *ssd_bus_init(Bus *b, uint num_channels, int64 ctrl_delay, int64 data_delay, uint table_size, uint max_connections);
Bus *ssd_bus_new(uint num_channels, int64 ctrl_delay, int64 data_delay, uint table_size, uint max_connections);
void ssd_bus_free(Bus *b);
enum status ssd_bus_connect(Bus *b, uint channel);
enum status ssd_bus_disconnect(Bus *b, uint channel);
enum status ssd_bus_lock(Bus *b, uint channel, int64 start_time, int64 duration, Event *event);
Channel *ssd_bus_get_channel(Bus *b, uint channel);



/* The page is the lowest level data storage unit that is the size unit of
 * requests (events).  Pages maintain their state as events modify them. */
typedef struct Page
{
    enum page_state state;
    const struct Block *parent;
    //int64 read_delay;
    //int64 write_delay;
/*  Address next_page; */
} Page;

Page *ssd_page_init (Page *p, const struct Block *parent, int64 read_delay, int64 write_delay);
Page *ssd_page_new (const struct Block *parent, int64 read_delay, int64 write_delay);
void ssd_page_free (Page *p);
enum status ssd_page_read(Page *p, Event *event);
enum status ssd_page_write(Page *p, Event *event);
const struct Block *ssd_page_get_parent(Page *p);
enum page_state ssd_page_get_state(Page *p);
void ssd_page_set_state(Page *p, enum page_state state);

/* The block is the data storage hardware unit where erases are implemented.
 * Blocks maintain wear statistics for the FTL. */
typedef struct Block
{
    Page **data;
    const struct Plane *parent;
    uint pages_valid;
    uint pages_invalid;
    enum block_state state;
    //uint size;
#ifdef HAS_WL
    uint erases_remaining;
#endif
    int64 last_erase_time;
    //int64 erase_delay;
} Block;

Block *ssd_block_new (const struct Plane *parent, uint block_size, ulong erases_remaining, int64 erase_delay);
void ssd_block_free(Block *b);
enum status ssd_block_read(Block *b, Event *event);
enum status ssd_block_write(Block *b, Event *event);
enum status ssd_block_erase(Block *b, Event *event);
const struct Plane *ssd_block_get_parent(Block *b);
uint ssd_block_get_pages_valid(Block *b);
uint ssd_block_get_pages_invalid(Block *b);
enum block_state ssd_block_get_state(Block *b);
enum page_state ssd_block_get_state2(Block *b, uint page);
enum page_state ssd_block_get_state3(Block *b, const Address *address);
int64 ssd_block_get_last_erase_time(Block *b);
ulong ssd_block_get_erases_remaining(Block *b);
uint ssd_block_get_size(Block *b);
void ssd_block_invalidate_page(Block *b, uint page);
enum status ssd_block_get_next_page(Block *b, Address *address);


/* The plane is the data storage hardware unit that contains blocks.
 * Plane-level merges are implemented in the plane.  Planes maintain wear
 * statistics for the FTL. */
typedef struct Plane {
    //uint size;
    Block ** data;
    const struct Die *parent;
    uint least_worn;
    uint free_blocks;
    ulong erases_remaining;
    int64 last_erase_time;
    //int64 reg_read_delay;
    //int64 reg_write_delay;
    Address next_page;
} Plane;

Plane *ssd_plane_new(const struct Die *parent, uint plane_size, int64 reg_read_delay, int64 reg_write_delay);
void ssd_plane_free(Plane *p);
enum status ssd_plane_read(Plane *p, Event *event);
enum status ssd_plane_write(Plane *p, Event *event);
enum status ssd_plane_erase(Plane *p, Event *event);
enum status ssd_plane_merge(Plane *p, Event *event);
uint ssd_plane_get_size(Plane *p);
const struct Die *ssd_plane_get_parent(Plane *p);
int64 ssd_plane_get_last_erase_time(Plane *p, const Address *address);
ulong ssd_plane_get_erases_remaining(Plane *p, const Address *address);
void ssd_plane_get_least_worn(Plane *p, Address *address);
enum page_state ssd_plane_get_state(Plane *p, const Address *address);
void ssd_plane_get_free_page(Plane *p, Address *address);
uint ssd_plane_get_num_free(Plane *p, const Address *address);
uint ssd_plane_get_num_valid(Plane *p, const Address *address);



/* The die is the data storage hardware unit that contains planes and is a flash
 * chip.  Dies maintain wear statistics for the FTL. */
typedef struct Die {
    uint size;
    Plane **data;
    const struct Package *parent;
    struct Channel *channel;
    uint least_worn;
    ulong erases_remaining;
    int64 last_erase_time;
} Die;

Die *ssd_die_init(Die *d, const struct Package *parent, struct Channel *channel, uint die_size);
Die *ssd_die_new(const struct Package *parent, struct Channel *channel, uint die_size);
void ssd_die_free(Die *d);
enum status ssd_die_read(Die *d, Event *event);
enum status ssd_die_write(Die *d, Event *event);
enum status ssd_die_erase(Die *d, Event *event);
enum status ssd_die_merge(Die *d, Event *event);
enum status ssd_die__merge(Die *d, Event *event);
const struct Package *ssd_die_get_parent(Die *d);
int64 ssd_die_get_last_erase_time(Die *d, const Address *address);
ulong ssd_die_get_erases_remaining(Die *d, const Address *address);
void ssd_die_get_least_worn(Die *d, Address *address);
enum page_state ssd_die_get_state(Die *d, const Address *address);
void ssd_die_get_free_page(Die *d, Address *address);
uint ssd_die_get_num_free(Die *d, const Address *address);
uint ssd_die_get_num_valid(Die *d, const Address *address);



/* The package is the highest level data storage hardware unit.  While the
 * package is a virtual component, events are passed through the package for
 * organizational reasons, including helping to simplify maintaining wear
 * statistics for the FTL. */
typedef struct Package
{
    uint size;
    Die **data;
    const struct Ssd *parent;
    uint least_worn;
    ulong erases_remaining;
    int64 last_erase_time;
} Package;
Package *ssd_package_new(const struct Ssd *parent, Channel *channel, uint package_size);
void ssd_package_free(Package *p);
enum status ssd_package_read(Package *p, Event *event);
enum status ssd_package_write(Package *p, Event *event);
enum status ssd_package_erase(Package *p, Event *event);
enum status ssd_package_merge(Package *p, Event *event);
const struct Ssd *ssd_package_get_parent(Package *p);
int64 ssd_package_get_last_erase_time(Package *p, const Address *address);
ulong ssd_package_get_erases_remaining(Package *p, const Address *address);
void ssd_package_get_least_worn(Package *p, Address *address);
enum page_state ssd_package_get_state(Package *p, const Address *address);
void ssd_package_get_free_page(Package *p, Address *address);
uint ssd_package_get_num_free(Package *p, const Address *address);
uint ssd_package_get_num_valid(Package *p, const Address *address);



/* The SSD is the single main object that will be created to simulate a real
 * SSD.  Creating a SSD causes all other objects in the SSD to be created.  The
 * event_arrive method is where events will arrive from DiskSim. */
typedef struct Ssd {
    uint size;
    Bus bus;
    Package **data;
    ulong erases_remaining;
    ulong least_worn;
    int64 last_erase_time;
} Ssd;
Ssd *ssd_sim_new(uint ssd_size);
void ssd_sim_free (Ssd *s);

#endif
