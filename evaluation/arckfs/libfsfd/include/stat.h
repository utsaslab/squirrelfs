#ifndef SUFS_LIBFS_STAT_H_
#define SUFS_LIBFS_STAT_H_

#include "../../include/libfs_config.h"
#include "tls.h"

enum sufs_libfs_timing_category
{
    SUFS_LIBFS_INDEX,
    SUFS_LIBFS_WRITEM,
    SUFS_LIBFS_TIMING_NUM,
};


extern const char *sufs_libfs_stats_string[SUFS_LIBFS_TIMING_NUM];


#if SUFS_LIBFS_STAT

#define SUFS_LIBFS_DEFINE_TIMING_VAR(name) unsigned long name

#define SUFS_LIBFS_START_TIMING(name, start)                                   \
    do {                                                                       \
            start = sufs_libfs_rdtscp();                                       \
        }  while (0)

#define SUFS_LIBFS_END_TIMING(name, start)                                     \
    do {                                                                       \
            unsigned long end;                                                 \
            int index = sufs_libfs_tls_my_index();                             \
            end = sufs_libfs_rdtscp();                                         \
            sufs_libfs_tls_data[index].stat.time[name] += end - start;         \
            sufs_libfs_tls_data[index].stat.count[name] += 1;                  \
    } while (0)

#else

#define SUFS_LIBFS_DEFINE_TIMING_VAR(x)
#define SUFS_LIBFS_START_TIMING(x, y)
#define SUFS_LIBFS_END_TIMING(x, y)


#endif


void sufs_libfs_print_timing_stats(void);

#endif
