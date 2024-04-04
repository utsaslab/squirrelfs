#ifndef SUFS_LIBFS_RANGE_LOCK_H_
#define SUFS_LIBFS_RANGE_LOCK_H_

#include "../../include/libfs_config.h"

struct sufs_libfs_irange_lock {
    struct sufs_libfs_bravo_rwlock *sg_table;
    long sg_size;
};

static inline void
sufs_libfs_irange_lock_init(struct sufs_libfs_irange_lock * lock)
{
    lock->sg_size = SUFS_LIBFS_SEGMENT_INIT_COUNT;
    lock->sg_table = calloc(SUFS_LIBFS_SEGMENT_INIT_COUNT,
            sizeof(struct sufs_libfs_bravo_rwlock));
}

static inline void
sufs_libfs_irange_lock_free(struct sufs_libfs_irange_lock *lock)
{
    if (lock->sg_table)
    {
        free(lock->sg_table);
    }
}


static inline void
sufs_libfs_irange_lock_read_lock(struct sufs_libfs_irange_lock *lock,
        unsigned long start_seg, unsigned long end_seg)
{
    unsigned long i = 0;

    if (lock->sg_table == NULL)
    {
        sufs_libfs_irange_lock_init(lock);
    }

    for (i = start_seg; i < end_seg; i++)
    {
        sufs_libfs_bravo_read_lock(&(lock->sg_table[i]));
    }
}

static inline void
sufs_libfs_irange_lock_read_unlock(struct sufs_libfs_irange_lock *lock,
        unsigned long start_seg, unsigned long end_seg)
{
    unsigned long i = 0;

    if (lock->sg_table == NULL)
    {
        sufs_libfs_irange_lock_init(lock);
    }

    for (i = start_seg; i < end_seg; i++)
    {
        sufs_libfs_bravo_read_unlock(&(lock->sg_table[i]));
    }
}

static inline void
sufs_libfs_irange_lock_write_lock(struct sufs_libfs_irange_lock *lock,
        unsigned long start_seg, unsigned long end_seg)
{
    unsigned long i = 0;

    if (lock->sg_table == NULL)
    {
        sufs_libfs_irange_lock_init(lock);
    }

    for (i = start_seg; i < end_seg; i++)
    {
        sufs_libfs_bravo_write_lock(&(lock->sg_table[i]));
    }
}

static inline void
sufs_libfs_irange_lock_write_unlock(struct sufs_libfs_irange_lock *lock,
        unsigned long start_seg, unsigned long end_seg)
{
    unsigned long i = 0;

    if (lock->sg_table == NULL)
    {
        sufs_libfs_irange_lock_init(lock);
    }

    for (i = start_seg; i < end_seg; i++)
    {
        sufs_libfs_bravo_write_unlock(&(lock->sg_table[i]));
    }
}

static inline void
sufs_libfs_irange_lock_resize(struct sufs_libfs_irange_lock *l, long new_size)
{
    int need_realloc = 0;

    if (l->sg_size == 0)
    {
        l->sg_size = 1;
    }

    while ( (new_size >> SUFS_LIBFS_SEGMENT_SIZE_BITS) >= l->sg_size)
    {
        l->sg_size *= 2;
        need_realloc = 1;
    }

    if (need_realloc)
    {
        if (l->sg_table)
        {
            free(l->sg_table);
        }

        l->sg_table= calloc(l->sg_size, sizeof(struct sufs_libfs_bravo_rwlock));
    }
}

#endif
