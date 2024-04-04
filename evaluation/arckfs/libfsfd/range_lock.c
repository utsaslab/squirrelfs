#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "../include/libfs_config.h"
#include "compiler.h"
#include "range_lock.h"
#include "util.h"

#define N                   9
#define CHECK_FOR_BIAS      16

/* BUG: Do not use __thread variable in sufs_libfs_code */
__thread int check_bias = 0;

static volatile unsigned long **global_vr_table;

void sufs_libfs_init_global_rglock_bravo(void)
{
    global_vr_table = calloc(SUFS_LIBFS_RL_TABLE_SIZE, sizeof(unsigned long *));
}

void sufs_libfs_free_global_rglock_bravo(void)
{
    if (global_vr_table)
        free(global_vr_table);

    global_vr_table = NULL;
}


static inline unsigned int mix32a(unsigned int v)
{
    static const unsigned int mix32ka = 0x9abe94e3 ;
    v = (v ^ (v >> 16)) * mix32ka ;
    v = (v ^ (v >> 16)) * mix32ka ;
    return v;
}

static inline unsigned int hash(unsigned long addr) {
    return mix32a((unsigned long) pthread_self() ^ addr) % SUFS_LIBFS_RL_NUM_SLOT;
}

static __always_inline int
atomic_add_unless(unsigned long *v, int a, int u)
{
    unsigned long c = (*v);

    do
    {
        if (unlikely(c == u))
            break;
    } while (!__sync_bool_compare_and_swap(v, &c, c + a));

    return c;
}

void sufs_libfs_irange_lock_read_lock(struct sufs_libfs_irange_lock *lock,
        unsigned long start, unsigned long size)
{
#if 0
    unsigned long trials;
#endif
    unsigned int wlock = (unsigned int)1 << 31;
    unsigned long i = 0, end = start + size;
    int old = 0;
    struct sufs_libfs_sg_entry *e = NULL;


    for (i = start; i < end; ++i)
    {
#if 0
        trials = 0;
#endif
        e = &lock->sg_table[i];

        if (e->rbias)
        {
            volatile unsigned long **slot = NULL;
            unsigned int id = hash((unsigned long)(&e->segment));
            slot = &global_vr_table[V(id)];

            if (__sync_val_compare_and_swap(slot, NULL, &e->segment) == NULL)
            {
                if (e->rbias)
                    continue;

                /*
                 * https://gcc.gnu.org/onlinedocs/gcc/_005f_005fsync-Builtins.html
                 * This built-in function, as described by Intel,
                 * is not a traditional test-and-set operation, but rather an
                 * atomic exchange operation. It writes value into *ptr, and
                 * returns the previous contents of *ptr.
                 */

                __sync_lock_test_and_set(slot, NULL);
            }
        }


        for (;;)
        {
#if 0
            trials++;
#endif

            old = atomic_add_unless(&e->segment, 1, wlock);

            if (old != 0)
                break;

#if 0
            if ((trials % MAX_TRY_COUNT) == 0) {
                if (trials == MAX_RETRIES_COUNT)
                    break;
            }
#endif
        }

        check_bias++;

        if ( (check_bias % CHECK_FOR_BIAS == 0) &&
                (!e->rbias && sufs_libfs_rdtsc() >= e->inhibit_until) )
        {
            e->rbias = 1;
        }

    }
}

void sufs_libfs_irange_lock_read_unlock(struct sufs_libfs_irange_lock *lock,
        unsigned long start, unsigned long size)
{
    unsigned long i = 0;
    unsigned long end = start + size;
    struct sufs_libfs_sg_entry *e = NULL;

    unsigned int id = 0;
    volatile unsigned long **slot = NULL;

    for (i = start; i < end; ++i)
    {
        e = &lock->sg_table[i];
        id = hash((unsigned long)(&e->segment));
        slot = &global_vr_table[V(id)];

        if (__sync_val_compare_and_swap(slot, &e->segment, NULL) == &e->segment)
            continue;

        __sync_fetch_and_add(&e->segment, -1);
    }
}

void sufs_libfs_irange_lock_write_lock(struct sufs_libfs_irange_lock *lock,
        unsigned long start, unsigned long size)
{
#if 0
    unsigned long trials;
#endif
    unsigned int wlock = (unsigned int) 1 << 31;
    unsigned long i = 0, end = start + size;
    struct sufs_libfs_sg_entry *e = NULL;

    unsigned int id = 0;
    volatile unsigned long *slot = NULL;

    for (i = start; i < end; ++i)
    {
#if 0
        trials = 0;
#endif
        e = &lock->sg_table[i];

        for (;;)
        {
#if 0
            trials++;
#endif

            if (__sync_bool_compare_and_swap(&e->segment, 0, wlock))
                break;

#if 0
            if ((trials % MAX_TRY_COUNT) == 0)
            {
                if (trials == MAX_RETRIES_COUNT)
                    break;
            }
#endif
        }


        id = hash((unsigned long)(&e->segment));
        slot = global_vr_table[V(id)];

        while (slot == &(e->segment));
    }
}

void sufs_libfs_irange_lock_write_unlock(struct sufs_libfs_irange_lock *lock,
        unsigned long start, unsigned long size)
{
#if 0
    unsigned long trials;
#endif
    unsigned int wlock = (unsigned int)1 << 31;
    unsigned long i = 0, end = start + size;
    struct sufs_libfs_sg_entry *e = NULL;

    for (i = start; i < end; i++)
    {
        e = &lock->sg_table[i];
#if 0
        trials = 0;
#endif

        for (;;)
        {
#if 0
            trials++;
#endif

            if (__sync_bool_compare_and_swap(&e->segment, wlock, 0))
                break;

#if 0
            if ((trials % MAX_TRY_COUNT) == 0) {
                if (trials == MAX_RETRIES_COUNT)
                    break;
            }
#endif
        }
    }
}

void sufs_libfs_irange_lock_resize(struct sufs_libfs_irange_lock *l,
        long new_size)
{
    int need_realloc = 0;

    while (new_size >> SUFS_LIBFS_SEGMENT_SIZE_BITS >= l->sg_size)
    {
        l->sg_size *= 2;
        need_realloc = 1;
    }

    if (need_realloc)
    {
        free(l->sg_table);
        l->sg_table= calloc(l->sg_size, sizeof(struct sufs_libfs_sg_entry));
    }
}

#if 0
void range_write_upgrade(struct range_lock *lock, unsigned long start,
        unsigned long size)
{
    /* Hold the lock from the beginning until the end */
    if (start != 0)
        range_write_lock(lock, 0, start - 1);
}

void range_write_downgrade(struct range_lock *lock, unsigned long start,
        unsigned long size)
{
    /* Release the lock from the beginning upto the start */
    if (start != 0)
        range_write_unlock(lock, 0, start - 1);
}
#endif



