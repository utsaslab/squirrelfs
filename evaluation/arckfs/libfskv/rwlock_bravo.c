/* copied somewhere online */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../include/libfs_config.h"
#include "rwlock_bravo.h"
#include "util.h"


#define BILLION    1000000000L
#define N          9
#define NR_ENTRIES 4096

static volatile struct sufs_libfs_bravo_rwlock_struct *visible_readers[NR_ENTRIES] =
{
    0,
};

static inline int mix32(unsigned long z)
{
    z = (z ^ (z >> 33)) * 0xff51afd7ed558ccdL;
    z = (z ^ (z >> 33)) * 0xc4ceb9fe1a85ec53L;
    return abs((int)(z >> 32));
}

static inline int bravo_hash(struct sufs_libfs_bravo_rwlock_struct *l)
{
    unsigned long a = pthread_self(),
                  b = (unsigned long) l;

    return mix32(a + b) % NR_ENTRIES;
}

void sufs_libfs_bravo_read_lock(struct sufs_libfs_bravo_rwlock_struct *l)
{
    unsigned long now_time;
    int slot;

    if (l->rbias)
    {
        slot = bravo_hash(l);

        if (__sync_bool_compare_and_swap(&visible_readers[slot], NULL, l))
        {
            if (l->rbias)
            {
                return;
            }

            visible_readers[slot] = NULL;
        }
    }

    /* slow-path */
    pthread_rwlock_rdlock(&l->underlying);

    now_time = sufs_libfs_rdtsc();

    if (l->rbias == false && now_time >= l->inhibit_until)
    {
        l->rbias = true;
    }
}

void sufs_libfs_bravo_read_unlock(struct sufs_libfs_bravo_rwlock_struct *l)
{
    int slot = 0;

    slot = bravo_hash(l);

    if (visible_readers[slot] != NULL)
    {
        visible_readers[slot] = NULL;
    }
    else
    {
        pthread_rwlock_unlock(&l->underlying);
    }
}

static inline void revocate(struct sufs_libfs_bravo_rwlock_struct *l)
{
    unsigned long start_time = 0, now_time = 0;
    int i = 0;

    l->rbias = false;

    start_time = sufs_libfs_rdtsc();

    for (i = 0; i < NR_ENTRIES; i++)
    {
        while (visible_readers[i] == l)
        {
            usleep(1);
        }
    }

    now_time = sufs_libfs_rdtsc();

    l->inhibit_until = now_time + ((now_time - start_time) * N);
}

void sufs_libfs_bravo_write_lock(struct sufs_libfs_bravo_rwlock_struct *l)
{
    pthread_rwlock_wrlock(&l->underlying);

    if (l->rbias)
    {
        revocate(l);
    }
}

#if 0

int bravo_read_trylock(bravo_rwlock_t *l) {
    struct timespec now;
    unsigned long now_time;
    int slot, s;

    if (l->rbias) {
        slot = bravo_hash(l);

        if (__sync_bool_compare_and_swap(&visible_readers[slot], NULL, l)) {
            if (l->rbias) {
                return 0;
            }

            visible_readers[slot] = NULL;
        }
    }

    /* slow-path */
    s = pthread_rwlock_tryrdlock(&l->underlying);
    if (__glibc_unlikely(s != 0)) {
        return s;
    }

    s = clock_gettime(CLOCK_MONOTONIC, &now);
    if (__glibc_unlikely(s != 0)) {
        perror("clock_gettime");
    }

    now_time = (now.tv_sec * BILLION) + now.tv_nsec;

    if (l->rbias == false && now_time >= l->inhibit_until) {
        l->rbias = true;
    }
    return 0;
}
int bravo_write_trylock(bravo_rwlock_t *l) {
    int s;

    s = pthread_rwlock_trywrlock(&l->underlying);
    if (__glibc_unlikely(s != 0)) {
        return s;
    }

    if (l->rbias) {
        revocate(l);
    }
    return 0;
}
#endif

