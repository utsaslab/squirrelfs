#ifndef SUFS_LIBFS_ATOMIC_UTIL_H_
#define SUFS_LIBFS_ATOMIC_UTIL_H_

#include <stdbool.h>

#include "../../include/libfs_config.h"
#include "filetable.h"

static inline bool sufs_libfs_cmpxch_bool(bool *ptr, bool expected, bool desired)
{
    return __atomic_compare_exchange_n(ptr, &expected, desired, 1,
    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline bool sufs_libfs_cmpxch_fdinfo(struct sufs_libfs_fdinfo *ptr,
        struct sufs_libfs_fdinfo expected, struct sufs_libfs_fdinfo desired)
{
    return __atomic_compare_exchange_n(&(ptr->data_), &(expected.data_),
            desired.data_, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

#endif /* SUFS_ATOMIC_UTIL_H_ */
