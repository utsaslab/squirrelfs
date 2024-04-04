#ifndef SUFS_LIBFS_ORIG_SYSCALL_H_
#define SUFS_LIBFS_ORIG_SYSCALL_H_

#include <unistd.h>

#include "../../include/libfs_config.h"

extern int (*sufs_libfs_orig_open)(const char *, int, ...);
extern ssize_t (*sufs_libfs_orig_read)(int, void *, size_t);
extern ssize_t (*sufs_libfs_orig_write)(int, const void *, size_t);
extern int (*sufs_libfs_orig_close)(int);

#endif
