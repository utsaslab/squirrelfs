#ifndef SUFS_LIBFS_AMD64_H_
#define SUFS_LIBFS_AMD64_H_

#include <stdint.h>

#include "../../include/libfs_config.h"

static inline void sufs_libfs_nop_pause(void)
{
    __asm volatile("pause" : :);
}

/* Atomically set bit nr of *a.  nr must be <= 64 */
static inline void sufs_libfs_locked_set_bit(uint64_t nr, volatile void *a)
{
    __asm volatile("lock; btsq %1,%0"
            : "+m"(*(volatile uint64_t *)a)
            : "lr"(nr)
            : "memory");
}

/* Atomically clear bit nr of *a.  nr must be <= 64 */
static inline void sufs_libfs_locked_reset_bit(uint64_t nr, volatile void *a)
{
    __asm volatile("lock; btrq %1,%0"
            : "+m"(*(volatile uint64_t *)a)
            : "lr"(nr)
            : "memory");
}

#endif
