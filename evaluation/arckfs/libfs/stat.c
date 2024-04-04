#include <stdio.h>

#include "../include/libfs_config.h"
#include "stat.h"
#include "tls.h"

const char * sufs_libfs_stats_string[SUFS_LIBFS_TIMING_NUM] =
{
        "index",
        "writem"
};



void sufs_libfs_print_timing_stats(void)
{
    int i = 0, j = 0;
    unsigned long times[SUFS_LIBFS_TIMING_NUM];
    unsigned long counts[SUFS_LIBFS_TIMING_NUM];


    for (i = 0; i < SUFS_LIBFS_TIMING_NUM; i++)
    {
        times[i] = 0;
        counts[i] = 0;
        for (j = 0; j < SUFS_MAX_THREADS; j++)
        {
            times[i] += sufs_libfs_tls_data[j].stat.time[i];
            counts[i] += sufs_libfs_tls_data[j].stat.count[i];
        }
    }

    printf("======== SUFS LIBS_TIMING STATS ========\n");
    for (i = 0; i < SUFS_LIBFS_TIMING_NUM; i++)
    {
        if (times[i])
        {
            printf("%s: count %lu, timing %lu, average %lu\n",
                   sufs_libfs_stats_string[i],
                   counts[i],
                   times[i],
                   counts[i] ? times[i] / counts[i] : 0);
        }
    }
}
