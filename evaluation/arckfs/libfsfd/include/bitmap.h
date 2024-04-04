#ifndef SUFS_LIBFS_BITMAP_H_
#define SUFS_LIBFS_BITMAP_H_

#include "../../include/libfs_config.h"
#include <stdatomic.h>

static inline void sufs_libfs_bm_set_bit(atomic_char * addr, int byte)
{
    int index = byte / 8;
    int offset = byte  % 8;

    addr[index] |= (1 << offset);
}

static inline void sufs_libfs_bm_clear_bit(atomic_char * addr, int byte)
{
    int index = byte / 8;
    int offset = byte  % 8;
    char mask = ~(1 << offset);

    addr[index] &= mask;
}

static inline int sufs_libfs_bm_test_bit(atomic_char * addr, int byte)
{
    int index = byte / 8;
    int offset = byte  % 8;

    return (addr[index] & (1 << offset));
}


#endif
