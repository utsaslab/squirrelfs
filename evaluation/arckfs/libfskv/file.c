#include <stdio.h>
#include <sys/stat.h>

#include "../include/libfs_config.h"
#include "../include/common_inode.h"
#include "file.h"
#include "mnode.h"

struct sufs_libfs_file_mnode*
sufs_libfs_file_mnode_init(struct sufs_libfs_mnode *m, bool r, bool w, bool a)
{
    struct sufs_libfs_file_mnode *ret = malloc(sizeof(struct sufs_libfs_mnode));

    ret->m = m;
    ret->off = 0;
    ret->append = a;
    ret->readable = r;
    ret->writable = w;

    return ret;
}

ssize_t sufs_libfs_file_mnode_read(struct sufs_libfs_file_mnode *f, char *addr,
        size_t n)
{
    ssize_t r = 0;
    u64 off = 0;

    if (!(f->readable))
        return -1;

    if (sufs_libfs_mnode_type(f->m) != SUFS_FILE_TYPE_REG)
    {
        return -1;
    }
    else
    {
#if 0
        printf("f->off is %ld, size is %ld\n", f->off,
                sufs_libfs_mnode_file_size(f->m));
#endif
        if (f->off >= sufs_libfs_mnode_file_size(f->m))
            return 0;

        off = f->off;
        r = sufs_libfs_readm(f->m, addr, off, n);
    }

    if (r > 0)
    {
        off += r;
        f->off = off;
    }

    return r;
}



ssize_t sufs_libfs_file_mnode_write(struct sufs_libfs_file_mnode *f, char *addr,
        size_t n)
{
    ssize_t r;
    u64 off;

    if (!(f->writable))
        return -1;

    if (sufs_libfs_mnode_type(f->m) != SUFS_FILE_TYPE_REG)
    {
        return -1;
    }
    else
    {
        if (f->append)
        {
            off = sufs_libfs_mnode_file_size(f->m);
        }
        else
        {
            off = f->off;
        }

        r = sufs_libfs_writem(f->m, addr, off, n);
    }

    if (r > 0)
    {
        off += r;
        f->off = off;
    }

    return r;
}

int sufs_libfs_file_mnode_truncate(struct sufs_libfs_file_mnode * f,
        off_t length)
{
    if (!(f->writable))
        return -1;

    if (sufs_libfs_mnode_type(f->m) != SUFS_FILE_TYPE_REG)
    {
        return -1;
    }

    return sufs_libfs_truncatem(f->m, length);
}

