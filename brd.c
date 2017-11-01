
/* 
 * Revised by Storage Research Group at Tsinghua University (thustorage)
 * http://storage.cs.tsinghua.edu.cn
 * 
 * (C) 2012 thustorage
 * Emulate Open-Channle SSD in the memory
 * part of the "Software Manged Flash" Project: http://storage.cs.tsinghua.edu.cn/~lu/research/smf.html
 *
 */


/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Ram backed block device driver.
 *
 * Copyright (C) 2007 Nick Piggin
 * Copyright (C) 2007 Novell Inc.
 *
 * Parts derived from drivers/block/rd.c, and drivers/block/loop.c, copyright
 * of their respective owners.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/highmem.h>
#include <linux/mutex.h>
#include <linux/radix-tree.h>
#include <linux/buffer_head.h> /* invalidate_bh_lrus() */
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/hrtimer.h>
#include <linux/version.h>
#include <asm/uaccess.h>

#include "ssd.h"

#define RAMSSD_MAJOR 10
#define SECTOR_SHIFT        9
#define PAGE_SECTORS_SHIFT  (PAGE_SHIFT - SECTOR_SHIFT)
#define PAGE_SECTORS        (1 << PAGE_SECTORS_SHIFT)


#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
#define _KERNEL_3_6_
#endif

#ifdef _KERNEL_3_6_
#define kunmap_atomic2(x,y) kunmap_atomic(x)
#define kmap_atomic2(x,y) kmap_atomic(x)
#else
#define kunmap_atomic2(x,y) kunmap_atomic(x,y)
#define kmap_atomic2(x,y) kmap_atomic(x,y)
#endif

static unsigned long sdk_capacity = 0;
spinlock_t      rq_lock;

struct brd_device *_brd = NULL;
static int g_total_w = 0;
#ifdef LONG_TERM_TIMER
struct hrtimer _timer;
#endif

static inline ktime_t my_ktime_sub (ktime_t ktime1, ktime_t ktime2)
{
    if (ktime_to_ns(ktime1) > ktime_to_ns(ktime2)) {
        return ktime_sub(ktime1, ktime2);
    } else
        return ktime_set (0, 101);
}
static inline ktime_t get_cur_ktime(struct hrtimer *timer)
{
    if (timer->base)
        return timer->base->get_time();
    else {
        struct timespec ts;
        do_posix_clock_monotonic_gettime(&ts);
        debug("######################################## timer=%lx\n", (long)timer);
        return ktime_set (ts.tv_sec, ts.tv_nsec);
    }
}

struct bio_queue {
    int ret;
    void *data;
    ktime_t ktime;
    struct bio_queue *next;
};

/*
 * Each block ramdisk device has a radix_tree brd_pages of pages that stores
 * the pages containing the block device's contents. A brd page's ->index is
 * its offset in PAGE_SIZE units. This is similar to, but in no way connected
 * with, the kernel's pagecache or buffer cache (which sit above our block
 * device).
 */
struct brd_device {
    int     brd_number;

    struct request_queue    *brd_queue;
    struct gendisk      *brd_disk;
    struct list_head    brd_list;

    /*
     * Backing store of pages and lock to protect it. This is the contents
     * of the block device.
     */
    spinlock_t      brd_lock;
    struct radix_tree_root  brd_pages;

    Ssd *ssd;
    struct bio_queue queue;
    struct mutex io_mutex;
    spinlock_t      queuelock;
    spinlock_t      simlock;
    struct hrtimer timer;
};


/*
 * Look up and return a brd's page for a given sector.
 */
static DEFINE_MUTEX(brd_mutex);
static struct page *brd_lookup_page(struct brd_device *brd, sector_t sector)
{
    pgoff_t idx;
    struct page *page;

    /*
     * The page lifetime is protected by the fact that we have opened the
     * device node -- brd pages will never be deleted under us, so we
     * don't need any further locking or refcounting.
     *
     * This is strictly true for the radix-tree nodes as well (ie. we
     * don't actually need the rcu_read_lock()), however that is not a
     * documented feature of the radix-tree API so it is better to be
     * safe here (we don't have total exclusion from radix tree updates
     * here, only deletes).
     */
    rcu_read_lock();
    idx = sector >> PAGE_SECTORS_SHIFT; /* sector to page index */
    page = radix_tree_lookup(&brd->brd_pages, idx);
    rcu_read_unlock();

    BUG_ON(page && page->index != idx);

    return page;
}

/*
 * Look up and return a brd's page for a given sector.
 * If one does not exist, allocate an empty page, and insert that. Then
 * return it.
 */
static struct page *brd_insert_page(struct brd_device *brd, sector_t sector)
{
    pgoff_t idx;
    struct page *page;
    gfp_t gfp_flags;

    page = brd_lookup_page(brd, sector);
    if (page)
        return page;

    /*
     * Must use NOIO because we don't want to recurse back into the
     * block or filesystem layers from page reclaim.
     *
     * Cannot support XIP and highmem, because our ->direct_access
     * routine for XIP must return memory that is always addressable.
     * If XIP was reworked to use pfns and kmap throughout, this
     * restriction might be able to be lifted.
     */
    gfp_flags = GFP_NOIO | __GFP_ZERO;
#ifndef CONFIG_BLK_DEV_XIP
    gfp_flags |= __GFP_HIGHMEM;
#endif
    page = alloc_page(gfp_flags);
    if (!page)
        return NULL;

    if (radix_tree_preload(GFP_NOIO)) {
        __free_page(page);
        return NULL;
    }

    spin_lock(&brd->brd_lock);
    idx = sector >> PAGE_SECTORS_SHIFT;
    if (radix_tree_insert(&brd->brd_pages, idx, page)) {
        __free_page(page);
        page = radix_tree_lookup(&brd->brd_pages, idx);
        BUG_ON(!page);
        BUG_ON(page->index != idx);
    } else
        page->index = idx;
    spin_unlock(&brd->brd_lock);

    radix_tree_preload_end();

    return page;
}

static void brd_free_page(struct brd_device *brd, sector_t sector)
{
    struct page *page;
    pgoff_t idx;

    spin_lock(&brd->brd_lock);
    idx = sector >> PAGE_SECTORS_SHIFT;
    page = radix_tree_delete(&brd->brd_pages, idx);
    spin_unlock(&brd->brd_lock);
    if (page)
        __free_page(page);
}

static void brd_zero_page(struct brd_device *brd, sector_t sector)
{
    struct page *page;

    page = brd_lookup_page(brd, sector);
    if (page)
        clear_highpage(page);
}

/*
 * Free all backing store pages and radix tree. This must only be called when
 * there are no other users of the device.
 */
#define FREE_BATCH 16
static void brd_free_pages(struct brd_device *brd)
{
    unsigned long pos = 0;
    struct page *pages[FREE_BATCH];
    int nr_pages;

    do {
        int i;

        nr_pages = radix_tree_gang_lookup(&brd->brd_pages,
                                          (void **)pages, pos, FREE_BATCH);

        for (i = 0; i < nr_pages; i++) {
            void *ret;

            BUG_ON(pages[i]->index < pos);
            pos = pages[i]->index;
            ret = radix_tree_delete(&brd->brd_pages, pos);
            BUG_ON(!ret || ret != pages[i]);
            __free_page(pages[i]);
        }

        pos++;

        /*
         * This assumes radix_tree_gang_lookup always returns as
         * many pages as possible. If the radix-tree code changes,
         * so will this have to.
         */
    } while (nr_pages == FREE_BATCH);
}

/*
 * copy_to_brd_setup must be called before copy_to_brd. It may sleep.
 */
static int copy_to_brd_setup(struct brd_device *brd, sector_t sector, size_t n)
{
    unsigned int offset = (sector & (PAGE_SECTORS-1)) << SECTOR_SHIFT;
    size_t copy;

    copy = min_t(size_t, n, PAGE_SIZE - offset);
    if (!brd_insert_page(brd, sector))
        return -ENOMEM;
    if (copy < n) {
        sector += copy >> SECTOR_SHIFT;
        if (!brd_insert_page(brd, sector))
            return -ENOMEM;
    }
    return 0;
}

static void discard_from_brd(struct brd_device *brd,
                             sector_t sector, size_t n)
{
    while (n >= PAGE_SIZE) {
        /*
         * Don't want to actually discard pages here because
         * re-allocating the pages can result in writeback
         * deadlocks under heavy load.
         */
        if (0)
            brd_free_page(brd, sector);
        else
            brd_zero_page(brd, sector);
        sector += PAGE_SIZE >> SECTOR_SHIFT;
        n -= PAGE_SIZE;
    }
}

/*
 * Copy n bytes from src to the brd starting at sector. Does not sleep.
 */
static void copy_to_brd(struct brd_device *brd, const void *src,
                        sector_t sector, size_t n)
{
    struct page *page;
    void *dst;
    unsigned int offset = (sector & (PAGE_SECTORS-1)) << SECTOR_SHIFT;
    size_t copy;

    copy = min_t(size_t, n, PAGE_SIZE - offset);
    page = brd_lookup_page(brd, sector);
    BUG_ON(!page);

    dst = kmap_atomic2(page, KM_USER1);
    memcpy(dst + offset, src, copy);
    kunmap_atomic2(dst, KM_USER1);

    if (copy < n) {
        src += copy;
        sector += copy >> SECTOR_SHIFT;
        copy = n - copy;
        page = brd_lookup_page(brd, sector);
        BUG_ON(!page);

        dst = kmap_atomic2(page, KM_USER1);
        memcpy(dst, src, copy);
        kunmap_atomic2(dst, KM_USER1);
    }
}

/*
 * Copy n bytes to dst from the brd starting at sector. Does not sleep.
 */
static void copy_from_brd(void *dst, struct brd_device *brd,
                          sector_t sector, size_t n)
{
    struct page *page;
    void *src;
    unsigned int offset = (sector & (PAGE_SECTORS-1)) << SECTOR_SHIFT;
    size_t copy;

    copy = min_t(size_t, n, PAGE_SIZE - offset);
    page = brd_lookup_page(brd, sector);
    if (page) {
        src = kmap_atomic2(page, KM_USER1);
        memcpy(dst, src + offset, copy);
        kunmap_atomic2(src, KM_USER1);
    } else
        memset(dst, 0, copy);

    if (copy < n) {
        dst += copy;
        sector += copy >> SECTOR_SHIFT;
        copy = n - copy;
        page = brd_lookup_page(brd, sector);
        if (page) {
            src = kmap_atomic2(page, KM_USER1);
            memcpy(dst, src, copy);
            kunmap_atomic2(src, KM_USER1);
        } else
            memset(dst, 0, copy);
    }
}

#ifndef NO_PERSIST
/*
 * Process a single bvec of a bio.
 */
static int brd_do_bvec(struct brd_device *brd, struct page *page,
                       unsigned int len, unsigned int off, int rw,
                       sector_t sector)
{
	//printk(KERN_ERR "IT is OK\n");
    void *mem;
    int err = 0, i, nsec = len >> 9;

    if (rw != READ) {
        for (i = 0; i < nsec; i++) {
            err = copy_to_brd_setup(brd, sector+i, 512);
            if (err)
                goto out;
        }
    }

    mem = kmap_atomic2(page, KM_USER0);
    debug("mem=%lx, off=%u, rw=%d, sec=%ld, len=%u, nsec=%d\n", (long)mem, off, rw, sector, len, nsec);
    if (rw == READ) {
        for (i = 0; i < nsec; i++)
            copy_from_brd(mem + off + (i << 9), brd, sector +i, 512);
        flush_dcache_page(page);
    } else {
        flush_dcache_page(page);
        for (i = 0; i < nsec; i++)
            copy_to_brd(brd, mem + off + (i<<9), sector+i, 512);
    }
    kunmap_atomic2(mem, KM_USER0);

out:
    return err;
}
#else
static int brd_do_bvec(struct brd_device *brd, struct page *page,
                       unsigned int len, unsigned int off, int rw,
                       sector_t sector)
{
	//printk(KERN_ERR "ZJC HAHAHAHAHAHAHAHAHAHAH\n");
    return 0;
}
#endif
static int add_io_timer (struct brd_device *brd, void *data, ktime_t uptime, ulong ns, int ret);
static void brd_make_request(struct request_queue *q, struct bio *bio)
{
    struct block_device *bdev = bio->bi_bdev;
    struct brd_device *brd = bdev->bd_disk->private_data;
    int rw;
    struct bio_vec *bvec;
    sector_t sector;
    int i;
    int err = -EIO;

#ifdef NOUSE_PRIVATE
    brd = _brd;
#endif

    debug("w=%lu %lu, sector=%lu\n", bio->bi_rw, bio_rw(bio), bio->bi_sector);

    sector = bio->bi_sector;
    if (sector + (bio->bi_size >> SECTOR_SHIFT) > sdk_capacity)
        goto out;

    if (unlikely(bio->bi_rw & REQ_DISCARD)) {
        err = 0;
        discard_from_brd(brd, sector, bio->bi_size);
        goto out;
    }

    rw = bio_rw(bio);
    if (rw == READA)
        rw = READ;

    bio_for_each_segment(bvec, bio, i) {
        unsigned int len = bvec->bv_len;
        err = brd_do_bvec(brd, bvec->bv_page, len,
                          bvec->bv_offset, rw, sector);
        if (err)
            break;
        sector += len >> SECTOR_SHIFT;
    }
out:
    debug("rw=%lu %lu, sector=%lu, err=%d\n", bio->bi_rw, bio_rw(bio), bio->bi_sector, err);
    bio_endio(bio, err);
}

#ifdef CONFIG_BLK_DEV_XIP
static int brd_direct_access(struct block_device *bdev, sector_t sector,
                             void **kaddr, unsigned long *pfn)
{
    struct brd_device *brd = bdev->bd_disk->private_data;
    struct page *page;

#ifdef NOUSE_PRIVATE
    brd = _brd;
#endif

    if (!brd)
        return -ENODEV;
    if (sector & (PAGE_SECTORS-1))
        return -EINVAL;
    if (sector + PAGE_SECTORS > get_capacity(bdev->bd_disk))
        return -ERANGE;
    page = brd_insert_page(brd, sector);
    if (!page)
        return -ENOMEM;
    *kaddr = page_address(page);
    *pfn = page_to_pfn(page);

    return 0;
}
#endif

static int brd_ioctl(struct block_device *bdev, fmode_t mode,
                     unsigned int cmd, unsigned long arg)
{
    int error;
    struct brd_device *brd = bdev->bd_disk->private_data;
#ifdef NOUSE_PRIVATE
    brd = _brd;
#endif

    if (cmd != BLKFLSBUF)
        return -ENOTTY;

    /*
     * ram device BLKFLSBUF has special semantics, we want to actually
     * release and destroy the ramdisk data.
     */
    mutex_lock(&brd_mutex);
    mutex_lock(&bdev->bd_mutex);
    error = -EBUSY;
    if (bdev->bd_openers <= 1) {
        /*
         * Invalidate the cache first, so it isn't written
         * back to the device.
         *
         * Another thread might instantiate more buffercache here,
         * but there is not much we can do to close that race.
         */
        invalidate_bh_lrus();
        truncate_inode_pages(bdev->bd_inode->i_mapping, 0);
        brd_free_pages(brd);
        error = 0;
    }
    mutex_unlock(&bdev->bd_mutex);
    mutex_unlock(&brd_mutex);

    return error;
}

static const struct block_device_operations brd_fops = {
    .owner =        THIS_MODULE,
    .ioctl =        brd_ioctl,
#ifdef CONFIG_BLK_DEV_XIP
    .direct_access =    brd_direct_access,
#endif
};

/*
 * And now the modules code and kernel interface.
 */
static int rd_nr;
int rd_size = CONFIG_BLK_DEV_RAM_SIZE;
static int max_part;
static int part_shift;
module_param(rd_nr, int, S_IRUGO);
MODULE_PARM_DESC(rd_nr, "Maximum number of brd devices");
module_param(rd_size, int, S_IRUGO);
MODULE_PARM_DESC(rd_size, "Size of each RAM disk in kbytes.");
module_param(max_part, int, S_IRUGO);
MODULE_PARM_DESC(max_part, "Maximum number of partitions per RAM disk");
MODULE_LICENSE("GPL");
MODULE_ALIAS_BLOCKDEV_MAJOR(RAMSSD_MAJOR);
MODULE_ALIAS("rd");

#ifndef MODULE
/* Legacy boot options - nonmodular */
static int __init ramdisk_size(char *str)
{
    rd_size = simple_strtol(str, NULL, 0);
    return 1;
}
__setup("ramdisk_size=", ramdisk_size);
#endif

/*
 * The device scheme is derived from loop.c. Keep them in synch where possible
 * (should share code eventually).
 */
static LIST_HEAD(brd_devices);
static DEFINE_MUTEX(brd_devices_mutex);

static int timer_cnt = 0;
static int g_req_pending = 0;
static int g_req_done = 0;

static int check_queue_length(struct brd_device *brd)
{
    int cnt = 0;
    struct bio_queue *p = &brd->queue;
    while (p->next != NULL) {
        cnt++;
        p = p->next;
    }
    return cnt;
}
static void handle_queue(struct brd_device *brd)
{
    unsigned long flags;
    struct bio_queue *p = &brd->queue;
    struct bio_queue *q = NULL;
    ktime_t ktime = get_cur_ktime(&brd->timer);

    spin_lock_irqsave(&brd->queuelock, flags);
    while (p->next != NULL) {
        struct bio_queue *tmp = p->next;
        if (ktime_to_ns(tmp->ktime) >  ktime_to_ns(ktime))
            break;

        p->next = tmp->next;
        tmp->next = q;
        q = tmp;
    }
    spin_unlock_irqrestore(&brd->queuelock, flags);

    while(q) {
        struct request *req = (struct request *)q->data;
        debug ("#%d End request %lx, ret=%d, lock=%lx\n", g_req_done++,(long)req, q->ret, (long)brd->brd_queue->queue_lock);
        __blk_end_request_all (req, q->ret);
        debug ("XXXXXXXXXXXXXXXXXXXXXXX\n");
        p = q;
        q = q->next;
        kfree (p);
    }
}

static int reset_timer(struct brd_device *brd)
{
    ktime_t ktime1, ktime0;
    ktime_t ktime;

    debug("%d,%d, qlen=%d\n", hrtimer_callback_running(&brd->timer), hrtimer_active(&brd->timer), check_queue_length(brd));
    if (brd->queue.next == NULL) {
        return HRTIMER_NORESTART;
    }

    ktime1 = brd->queue.next->ktime;
    ktime0 = get_cur_ktime(&brd->timer);
    ktime = my_ktime_sub(ktime1, ktime0);
    debug("ktime = %llu, %llu, %llu nsec\n", ktime0.tv64, ktime1.tv64, ktime.tv64);

    if (!hrtimer_active(&brd->timer)) {
        debug("%s\n", "start timer");
        hrtimer_start (&brd->timer, ktime, HRTIMER_MODE_REL);
    } else if (hrtimer_callback_running(&brd->timer)){
        debug("%s\n", "forward timer");
        hrtimer_forward_now(&brd->timer, ktime);
    }
    return HRTIMER_RESTART;
}

static enum hrtimer_restart hrtimer_callback(struct hrtimer *timer)
{
    unsigned long flags;

    debug("Time:%llu\n", ktime_to_ns(get_cur_ktime(timer)));
    if (!_brd)
        return HRTIMER_NORESTART;

    //if (timer_cnt % 1000 == 0)
    debug("timer_cnt=%d, qlen=%d\n", timer_cnt, check_queue_length(_brd));
    timer_cnt++;
    if (spin_trylock_irqsave(_brd->brd_queue->queue_lock, flags)) {
        handle_queue (_brd);
        spin_unlock_irqrestore(_brd->brd_queue->queue_lock, flags);
        debug("qlen=%d\n", check_queue_length(_brd));
    }
    return reset_timer(_brd);
}

#ifdef LONG_TERM_TIMER
static enum hrtimer_restart hrtimer_callback2(struct hrtimer *timer)
{
    static int cnt = 0;
    unsigned long flags;
    ktime_t ktime = ktime_set(0, DEFAULT_TIMEOUT);
    ++cnt;
    if (!_brd) {
        printk(KERN_INFO "ramssd: brd null\n");
        return HRTIMER_NORESTART;
    }
    if (cnt % 1000 == 0)
        debug("%d Time: qlen=%d\n", cnt, check_queue_length(_brd));

    if (spin_trylock_irqsave(_brd->brd_queue->queue_lock, flags)) {
        handle_queue (_brd);
        spin_unlock_irqrestore(_brd->brd_queue->queue_lock, flags);
    }

    if (_brd->queue.next != NULL) {
        ktime  = _brd->queue.next->ktime;
        ktime = my_ktime_sub(ktime, get_cur_ktime(timer));
    }
    hrtimer_forward_now(timer, ktime);
    check_queue_length(_brd);
    return HRTIMER_RESTART;
}
#endif

static int add_to_queue (struct brd_device *brd, struct bio_queue *q)
{
    struct bio_queue *p = &brd->queue;

    while (p->next != NULL) {
        if (ktime_to_ns(p->next->ktime) > ktime_to_ns(q->ktime))
            break;
        p = p->next;
    }
    q->next = p->next;
    p->next = q;
    return (brd->queue.next == q);
}

static int add_io_timer (struct brd_device *brd, void *data, ktime_t uptime, ulong ns, int ret)
{
    unsigned long flags;
    struct bio_queue *q = (struct bio_queue *)kmalloc(sizeof(struct bio_queue), GFP_KERNEL);
    if (!q)
        return -1;

    q->data = data;
    q->ret = ret;
    q->ktime = ktime_add_ns(uptime, ns);

    spin_lock_irqsave(&brd->queuelock, flags);
    add_to_queue (brd, q);
    reset_timer(brd);
    spin_unlock_irqrestore(&brd->queuelock, flags);
    return 0;
}
static void print_BIO(struct bio* bio)
{
	int index,i;
	int rw=0;

	if(bio->bi_rw&WRITE)
		rw=1;

	if(rw)
		printk(KERN_ERR " Write Rquest %lx %lx %x sectors flg %lx\n", (long)bio, bio->bi_sector, bio_sectors(bio), bio->bi_flags);
	else
		printk(KERN_ERR " Read Rquest %lx %lx %x sectors flg %lx\n", (long)bio, bio->bi_sector, bio_sectors(bio), bio->bi_flags);
	
	for(index=bio->bi_idx;index<bio->bi_vcnt;index++)
	{
		printk(KERN_ERR "index=%d\tcount=%d\n",index,bio->bi_vcnt);
		unsigned int len= bio_iovec_idx(bio,index)->bv_len;
		char* p=(char*)(page_address(bio_iovec_idx(bio,index)->bv_page) +bio_iovec_idx(bio,index)->bv_offset);
		for(i=0;i<len;i++,p++)
			printk(KERN_ERR "%d",*p);
		printk(KERN_ERR "\n");
		
	}
}

static int handle_request_bio(struct request_queue *q, struct bio *bio)
{
    struct block_device *bdev = bio->bi_bdev;
    struct brd_device *brd = bdev->bd_disk->private_data;
    int rw;
    struct bio_vec *bvec;
    sector_t sector;
    int i;
    int err = -EIO;

#ifdef NOUSE_PRIVATE
    brd = _brd;
#endif

    sector = bio->bi_sector;
    if (sector + (bio->bi_size >> SECTOR_SHIFT) > sdk_capacity)
        goto out;

    if (unlikely(bio->bi_rw & REQ_DISCARD)) {
        err = 0;
        discard_from_brd(brd, sector, bio->bi_size);
        goto out;
    }

    rw = bio_rw(bio);
    if (rw == READA)
        rw = READ;

    bio_for_each_segment(bvec, bio, i) {
        unsigned int len = bvec->bv_len;
        err = brd_do_bvec(brd, bvec->bv_page, len,
                          bvec->bv_offset, rw, sector);
        if (err)
            break;
        sector += len >> SECTOR_SHIFT;
    }
out:
    debug("rw=%lu, sector=%lu, err=%d\n", bio_rw(bio), bio->bi_sector, err);
    return err;
}

#include "settings.h"
//#define TEST_SYNC
static void do_request(struct request_queue *q)
{
    unsigned int block, nsect;
    struct request *req = NULL;
    struct bio *bio = NULL;
    struct brd_device *brd = NULL;
    int ret = 0;
    int64_t timeval = 0, start_time = 0;
    unsigned long logical_address = 0;
    unsigned int size = 0;
    int type = 0, i;
    ktime_t uptime;

repeat:
    if (!req) {
        req = blk_fetch_request(q);
        if (!req) {
            debug ("no req cur=%lx, q=%lx\n", current, q);
            return;
        }
    }

    brd = req->rq_disk->private_data;
#ifdef NOUSE_PRIVATE
    brd = _brd;
#endif

    block = blk_rq_pos(req);
    nsect = blk_rq_sectors(req);
    debug("bio=%lx, blk=%u,%u nsec=%u,cur=%lx, gd=%lx, q=%lx, %lx\n", req->bio, block, block/9, nsect, current, req->rq_disk, req->rq_disk->queue, q);
    if (get_capacity(req->rq_disk) == 0) {
        warning (KERN_ERR "ERROR: rq disk capactiy 0 is %lx\n", (long)req->rq_disk);
    }
    if (block >= sdk_capacity ||
        ((block+nsect) > sdk_capacity)) {
        warning("%s: bad access: block=%d, count=%d, capaciy=%lu\n",
                req->rq_disk->disk_name, block, nsect,
                sdk_capacity);
        blk_end_request (req, -EIO, blk_rq_bytes(req));
        req = NULL;
        goto repeat;
    }

    handle_queue (brd);
    spin_unlock(&rq_lock);

    __rq_for_each_bio(bio, req) {
        mutex_lock(&brd->io_mutex);
        if (!(bio->bi_flags & (1 << BIO_CLONED)) && !(bio->bi_rw & REQ_DISCARD) && (bio->bi_rw & 0x1))
            ret = -111;
        else
            ret = handle_request_bio(q, bio);
        mutex_unlock(&brd->io_mutex);
        if (ret < 0) {
            if (ret == -111)
                warning("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!flag=%lx, rw=%lx\n", bio->bi_flags, bio->bi_rw);
            break;
        }
    }

    if (nsect == 1 || ret < 0) {
        blk_end_request (req, ret, blk_rq_bytes(req));
        goto out;
    }
    logical_address = block/SSD_PAGE_OOBSECS;
    if (nsect%SSD_PAGE_OOBSECS == 0)
        size = nsect * SSD_PAGE_SECS/SSD_PAGE_OOBSECS;
    else
        size = nsect;

    if (unlikely(req->bio->bi_rw & REQ_DISCARD)) {
        type = SSD_ERASE;
    } else if (req->bio->bi_rw & 0x1) {
        type = SSD_WRITE;
    } else {
        type = SSD_READ;
    }

    uptime = get_cur_ktime(&brd->timer);
    start_time = ktime_to_ns(uptime);
    if (page_reserved(sdk_capacity, logical_address))
        timeval = 100;
    else {
        uint num = size >> (FLASHPGSZBIT-9);
        uint unit = 1;
        int64_t _tmp = 0;
        timeval = 0;
        if (type == SSD_ERASE) {
            num = num >> (FLASHPG_NUM_BLOCK_SHIFT);
            unit = FLASHPGS_PER_BLOCK;
        }
        spin_lock(&brd->simlock);
        for (i = 0; i < num; ++i) {
            _tmp = ssd_event_arrive (brd->ssd, type, logical_address+i*unit, size, start_time);
            if (_tmp < 0) {
                timeval = -1;
                break;
            }
            if (_tmp > timeval)
                timeval = _tmp;
        }
        spin_unlock(&brd->simlock);
        if (type == SSD_WRITE)
            g_total_w += num;
    }
    if (timeval < 0) {
        timeval = 100;
        ret = -2;
        warning("#%d req=%lx, type=%d, block=%u, nsect=%u,laddr=%lu, size=%d pages\n", g_req_pending++, (long)req, type, block, nsect, logical_address, size/SSD_PAGE_SECS);
    }
#ifdef TEST_SYNC
    blk_end_request (req, ret, blk_rq_bytes(req));
#else
    add_io_timer(brd, req, uptime, timeval, ret);
#endif

out:
    debug("type=%d, vpn=%lu, size=%d, tv=%llu ns %llu us\n", type, logical_address, size, timeval, start_time/1000);
    spin_lock(&rq_lock);
    req = NULL;
    goto repeat;
}

static struct brd_device *brd_alloc(int i)
{
    struct brd_device *brd;
    struct gendisk *disk;

    brd = kzalloc(sizeof(*brd), GFP_KERNEL);
    if (!brd)
        goto out;
    brd->brd_number     = i;
    spin_lock_init(&brd->brd_lock);
    spin_lock_init(&brd->queuelock);
    spin_lock_init(&brd->simlock);
    INIT_RADIX_TREE(&brd->brd_pages, GFP_ATOMIC);
    mutex_init (&brd->io_mutex);

    brd->brd_queue = blk_init_queue (do_request, &rq_lock);
    if (!brd->brd_queue)
        goto out_free_dev;
    blk_queue_max_hw_sectors(brd->brd_queue, 1024*256);
    blk_queue_logical_block_size(brd->brd_queue, 512);
    queue_flag_set_unlocked(QUEUE_FLAG_NOMERGES, brd->brd_queue);
    queue_flag_set_unlocked (QUEUE_FLAG_DISCARD, brd->brd_queue);
    blk_queue_max_discard_sectors (brd->brd_queue, 1 << 16);

    disk = brd->brd_disk = alloc_disk(1 << part_shift);
    if (!disk)
        goto out_free_queue;

    brd->ssd = ssd_sim_new(SSD_SIZE); // 512MB page_size=4KB
    if (!brd->ssd)
        goto out_free_disk;
    brd->queue.ktime = ktime_set(0,0);
    brd->queue.next = NULL;
    hrtimer_init(&brd->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    brd->timer.function = hrtimer_callback;

    disk->major     = RAMSSD_MAJOR;
    disk->first_minor   = i << part_shift;
    disk->fops      = &brd_fops;
    disk->private_data  = brd;
    disk->queue     = brd->brd_queue;
    disk->flags |= GENHD_FL_SUPPRESS_PARTITION_INFO;
    sprintf(disk->disk_name, "ramssd%d", i);
    set_capacity(disk, rd_size);

    sdk_capacity = get_capacity(disk);
    printk(KERN_INFO "ramssd: disk %lx capacity = %lu, %d %dMB, major=%d %d\n", (long)disk, get_capacity(disk), RAM_SSD_SIZE, RAM_SSD_SIZE/2048, disk->major, disk->first_minor);
    printk(KERN_INFO "queue_lock=%lx, rq_lock=%lx, brd->timer=%lx\n", (long)brd->brd_queue->queue_lock, (long)&rq_lock, (long)&brd->timer);
    return brd;

out_free_disk:
    put_disk(brd->brd_disk);
out_free_queue:
    blk_cleanup_queue(brd->brd_queue);
out_free_dev:
    kfree(brd);
out:
    return NULL;
}

static void brd_free(struct brd_device *brd)
{
    put_disk(brd->brd_disk);
    blk_cleanup_queue(brd->brd_queue);
    brd_free_pages(brd);
    kfree(brd);
}

static struct brd_device *brd_init_one(int i)
{
    struct brd_device *brd;

    list_for_each_entry(brd, &brd_devices, brd_list) {
        if (brd->brd_number == i)
            goto out;
    }

    brd = brd_alloc(i);
    if (brd) {
        add_disk(brd->brd_disk);
        list_add_tail(&brd->brd_list, &brd_devices);
    }
out:
    return brd;
}

static void brd_del_one(struct brd_device *brd)
{
    list_del(&brd->brd_list);
    ssd_sim_free (brd->ssd);
    hrtimer_cancel(&brd->timer);
    handle_queue(brd);
    del_gendisk(brd->brd_disk);
    brd_free(brd);
}

static struct kobject *brd_probe(dev_t dev, int *part, void *data)
{
    struct brd_device *brd;
    struct kobject *kobj;

    mutex_lock(&brd_devices_mutex);
    brd = brd_init_one(MINOR(dev) >> part_shift);
    kobj = brd ? get_disk(brd->brd_disk) : ERR_PTR(-ENOMEM);
    mutex_unlock(&brd_devices_mutex);

    *part = 0;
    return kobj;
}

static int __init brd_init(void)
{
    int i, nr;
    unsigned long range;
    struct brd_device *brd, *next;

    /*
     * brd module now has a feature to instantiate underlying device
     * structure on-demand, provided that there is an access dev node.
     * However, this will not work well with user space tool that doesn't
     * know about such "feature".  In order to not break any existing
     * tool, we do the following:
     *
     * (1) if rd_nr is specified, create that many upfront, and this
     *     also becomes a hard limit.
     * (2) if rd_nr is not specified, create CONFIG_BLK_DEV_RAM_COUNT
     *     (default 16) rd device on module load, user can further
     *     extend brd device by create dev node themselves and have
     *     kernel automatically instantiate actual device on-demand.
     */

    part_shift = 0;
    if (max_part > 0) {
        part_shift = fls(max_part);

        /*
         * Adjust max_part according to part_shift as it is exported
         * to user space so that user can decide correct minor number
         * if [s]he want to create more devices.
         *
         * Note that -1 is required because partition 0 is reserved
         * for the whole disk.
         */
        max_part = (1UL << part_shift) - 1;
    }

    if ((1UL << part_shift) > DISK_MAX_PARTS)
        return -EINVAL;

    if (rd_nr > 1UL << (MINORBITS - part_shift))
        return -EINVAL;

    if (rd_nr && rd_nr != -1) {
        printk(KERN_INFO "ramssd: only one disk is supportted\n");
        return -1;
    }
    rd_nr = 1;

    rd_size = RAM_SSD_SIZE;
    printk(KERN_INFO "ramssd:part=%d, shift=%d, nr=%d, rd_size=%d, %dMB\n", max_part, part_shift, rd_nr, rd_size, rd_size >> 21);
    if (rd_nr) {
        nr = rd_nr;
        range = rd_nr << part_shift;
    } else {
        nr = CONFIG_BLK_DEV_RAM_COUNT;
        range = 1UL << MINORBITS;
    }

    if (register_blkdev(RAMSSD_MAJOR, "ramssd"))
        return -EIO;

    spin_lock_init(&rq_lock);
    for (i = 0; i < nr; i++) {
        brd = brd_alloc(i);
        if (!brd)
            goto out_free;
        _brd = brd;
        list_add_tail(&brd->brd_list, &brd_devices);
    }

    /* point of no return */

    list_for_each_entry(brd, &brd_devices, brd_list)
        add_disk(brd->brd_disk);

    blk_register_region(MKDEV(RAMSSD_MAJOR, 0), range,
                        THIS_MODULE, brd_probe, NULL, NULL);

#ifdef LONG_TERM_TIMER
    hrtimer_init(&_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    _timer.function = hrtimer_callback2;
    hrtimer_start (&_timer, ktime_set(0,1000), HRTIMER_MODE_REL);
#endif

    printk(KERN_INFO "ramssd: module loaded\n");
    printk(KERN_INFO "jiffies=%ld HZ=%d\n", jiffies, HZ);

    return 0;

out_free:
    list_for_each_entry_safe(brd, next, &brd_devices, brd_list) {
        list_del(&brd->brd_list);
        brd_free(brd);
    }
    unregister_blkdev(RAMSSD_MAJOR, "ramssd");

    return -ENOMEM;
}

static void __exit brd_exit(void)
{
    unsigned long range;
    struct brd_device *brd, *next;

    printk(KERN_INFO "ramssd: module unloaded g_total_w=%d\n", g_total_w);

    range = rd_nr ? rd_nr << part_shift : 1UL << MINORBITS;

#ifdef LONG_TERM_TIMER
    hrtimer_cancel(&_timer);
#endif
    list_for_each_entry_safe(brd, next, &brd_devices, brd_list)
        brd_del_one(brd);

    blk_unregister_region(MKDEV(RAMSSD_MAJOR, 0), range);
    unregister_blkdev(RAMSSD_MAJOR, "ramssd");
}

module_init(brd_init);
module_exit(brd_exit);

