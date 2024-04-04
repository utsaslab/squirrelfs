#ifndef SUFS_LIBFS_TLS_H_
#define SUFS_LIBFS_TLS_H_

#define _GNU_SOURCE
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>

#include "../../include/libfs_config.h"
#include "util.h"

/* TODO: Make this adjust to sufs_libfs_timing_category */
/* Please make sure the below struct is cache-aligned */
struct sufs_libfs_stat
{
    unsigned long time[4];
    unsigned long count[4];
};


struct sufs_libfs_tls
{
    int rand_seed;
    int cpt_idx;
    struct sufs_notifyer * cpt_cnt;

    char pad[48];

    struct sufs_libfs_stat stat;
};

extern int sufs_libfs_btid;
extern struct sufs_libfs_tls sufs_libfs_tls_data[SUFS_MAX_THREADS];
extern __thread int sufs_libfs_my_thread;

static inline int sufs_libfs_tls_my_index(void)
{
    int ret = sufs_libfs_my_thread;

    if (ret == -1)
    {
        sufs_libfs_my_thread = sufs_libfs_gettid() - sufs_libfs_btid;
        ret = sufs_libfs_my_thread;
    }

    if (ret >= SUFS_MAX_THREADS)
        abort();

    return ret;
}

#if 0
static inline int sufs_libfs_tls_my_index(void)
{
    int ret = sufs_libfs_gettid() - sufs_libfs_btid;

    if (ret >= SUFS_MAX_THREADS)
        abort();

    return ret;
}
#endif

void sufs_libfs_tls_init(void);

#endif /* SUFS_LIBFS_TLS_H_ */
