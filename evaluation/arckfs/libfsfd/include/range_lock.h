#ifndef SUFS_LIBFS_RANGE_LOCK_H_
#define SUFS_LIBFS_RANGE_LOCK_H_

#include "../../include/libfs_config.h"

/* Ad-hoc code to work with */
#if 0
#define MAX_TRY_COUNT       100000
#define MAX_RETRIES_COUNT   1000000000
#endif

#define V(i)        ((i))

struct sufs_libfs_sg_entry {
    unsigned long segment;
    volatile int rbias;
    volatile unsigned long inhibit_until;
};

struct sufs_libfs_irange_lock {
    struct sufs_libfs_sg_entry *sg_table;
    long sg_size;

};

static inline void
sufs_libfs_irange_lock_init(struct sufs_libfs_irange_lock * lock)
{
    lock->sg_size = SUFS_LIBFS_SEGMENT_INIT_COUNT;
    lock->sg_table = calloc(SUFS_LIBFS_SEGMENT_INIT_COUNT,
                            sizeof(struct sufs_libfs_sg_entry));
}

static inline void
sufs_libfs_irange_lock_free(struct sufs_libfs_irange_lock *lock)
{
    free(lock->sg_table);
}

void sufs_libfs_init_global_rglock_bravo(void);

void sufs_libfs_free_global_rglock_bravo(void);

void sufs_libfs_irange_lock_read_lock(struct sufs_libfs_irange_lock *lock,
        unsigned long start, unsigned long size);

void sufs_libfs_irange_lock_read_unlock(struct sufs_libfs_irange_lock *lock,
        unsigned long start, unsigned long size);

void sufs_libfs_irange_lock_write_lock(struct sufs_libfs_irange_lock *lock,
        unsigned long start, unsigned long size);

void sufs_libfs_irange_lock_write_unlock(struct sufs_libfs_irange_lock *lock,
        unsigned long start, unsigned long size);

void sufs_libfs_irange_lock_resize(struct sufs_libfs_irange_lock *l,
        long new_size);

#endif
