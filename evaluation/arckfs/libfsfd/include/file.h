#ifndef SUFS_LIBFS_FILE_H_
#define SUFS_LIBFS_FILE_H_

#include <sys/types.h>
#include <stdbool.h>

#include "../../include/libfs_config.h"
#include "bitmap.h"
#include "types.h"
#include "mfs.h"

struct sufs_libfs_file_mnode
{
        struct sufs_libfs_mnode *m;
        u64 off;
        bool readable;
        bool writable;
        bool append;
};

static inline ssize_t sufs_libfs_file_mnode_pread(
        struct sufs_libfs_file_mnode *f, char *addr, size_t n, off_t off)
{
    if (!(f->readable))
        return -1;

    return sufs_libfs_readm(f->m, addr, off, n);
}

static inline ssize_t sufs_libfs_file_mnode_pwrite(
        struct sufs_libfs_file_mnode *f, char *addr, size_t n, off_t off)
{
    if (!(f->writable))
        return -1;

    return sufs_libfs_writem(f->m, addr, off, n);
}

struct sufs_libfs_file_mnode* sufs_libfs_file_mnode_init(
        struct sufs_libfs_mnode *m, bool r, bool w, bool a);

ssize_t sufs_libfs_file_mnode_read(struct sufs_libfs_file_mnode *f, char *addr,
        size_t n);

ssize_t sufs_libfs_file_mnode_write(struct sufs_libfs_file_mnode *f, char *addr,
        size_t n);

int sufs_libfs_file_mnode_truncate(struct sufs_libfs_file_mnode * f,
        off_t length);

#endif
