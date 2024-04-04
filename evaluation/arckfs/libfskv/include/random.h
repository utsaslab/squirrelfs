#ifndef SUFS_LIBFS_RANDOM_H_
#define SUFS_LIBFS_RANDOM_H_

#include <pthread.h>

#include "../../include/libfs_config.h"
#include "util.h"
#include "types.h"
#include "tls.h"

static inline u32 sufs_libfs_xor_random(void)
{
    u32 v;
    int idx = 0;

    idx = sufs_libfs_tls_my_index();

    v = sufs_libfs_tls_data[idx].rand_seed;

    if (v == 0)
    {
        v = pthread_self();
    }

    v ^= v << 6;
    v ^= v >> 21;
    v ^= v << 7;

    sufs_libfs_tls_data[idx].rand_seed = v;

    return v;
}

#if 0
static inline u32 sufs_libfs_xor_random(void)
{
    u32 v;

    v = sufs_libfs_rand_seed;

    if (v == 0)
    {
//        v = pthread_self();
        v = sufs_libfs_rdtsc() & 0xffffffff;
    }

    v = 214013 * v + 2531011;
    v = (v >> 16) & 0x7FFF;

    return v;
}
#endif

#endif
