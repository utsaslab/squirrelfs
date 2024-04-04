#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../include/libfs_config.h"
#include "rwlock_bravo.h"
#include "bravo.h"
#include "util.h"


void sufs_libfs_bravo_read_lock(struct sufs_libfs_bravo_rwlock *l)
{
    unsigned long now_time;
    int slot;

    if (l->rbias)
    {
        slot = sufs_libfs_bravo_hash((unsigned long ) l);

        if (__sync_bool_compare_and_swap(&sufs_libfs_global_vr_table[slot], NULL, l))
        {
            if (l->rbias)
            {
                return;
            }

            sufs_libfs_global_vr_table[slot] = NULL;
        }
    }

    /* slow-path */
    sufs_libfs_rwticket_rdlock(&l->underlying);

    now_time = sufs_libfs_rdtsc();

    if (l->rbias == false && now_time >= l->inhibit_until)
    {
        l->rbias = true;
    }
}

void sufs_libfs_bravo_read_unlock(struct sufs_libfs_bravo_rwlock *l)
{
    int slot = 0;

    slot = sufs_libfs_bravo_hash((unsigned long) l);

    if (sufs_libfs_global_vr_table[slot] != NULL)
    {
        sufs_libfs_global_vr_table[slot] = NULL;
    }
    else
    {
        sufs_libfs_rwticket_rdunlock(&l->underlying);
    }
}

void sufs_libfs_bravo_write_lock(struct sufs_libfs_bravo_rwlock *l)
{
    sufs_libfs_rwticket_wrlock(&l->underlying);

    if (l->rbias)
    {
        unsigned long start_time = 0, now_time = 0, i = 0;

        l->rbias = false;

        start_time = sufs_libfs_rdtsc();

        for (i = 0; i < SUFS_LIBFS_RL_NUM_SLOT; i++)
        {
            while (sufs_libfs_global_vr_table[i] == (unsigned long *) l);
        }

        now_time = sufs_libfs_rdtsc();

        l->inhibit_until = now_time + ((now_time - start_time) * SUFS_LIBFS_BRAVO_N);
    }
}
