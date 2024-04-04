#ifndef SUFS_LIBFS_BRAVO_H_
#define SUFS_LIBFS_BRAVO_H_

#include "../../include/libfs_config.h"

static inline unsigned long sufs_libfs_hash_int(unsigned long x)
{
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ul;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebul;
    x = x ^ (x >> 31);
    return x;
}

static inline unsigned int sufs_libfs_bravo_hash(unsigned long addr)
{
    return sufs_libfs_hash_int( ((unsigned long) pthread_self()) + addr)
            % SUFS_LIBFS_RL_NUM_SLOT;
}

extern volatile unsigned long **sufs_libfs_global_vr_table;

void sufs_libfs_init_global_rglock_bravo(void);

void sufs_libfs_free_global_rglock_bravo(void);

#endif
