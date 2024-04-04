#ifndef SUFS_GLOBAL_COMMON_INODE_H_
#define SUFS_GLOBAL_COMMON_INODE_H_

#include "config.h"

#define SUFS_INODE_TOMBSTONE  1

#define SUFS_FILE_TYPE_NONE 0
#define SUFS_FILE_TYPE_REG  1
#define SUFS_FILE_TYPE_DIR  2

struct sufs_inode
{
    char file_type;
    unsigned int mode;
    unsigned int uid;
    unsigned int gid;
    unsigned long size;
    unsigned long offset;
    long atime;
    long ctime;
    long mtime;
};

struct sufs_dir_entry
{
    char name_len;
    int ino_num;
    short rec_len;
    struct sufs_inode inode;
    char name[];
};

/* Make sure this is a multiple of page size */
struct sufs_fidx_entry
{
    /* offset from the beginning of the first virtual address of PM */
    unsigned long offset;
};


#define sufs_is_norm_fidex(x) \
    (((unsigned long) x + sizeof(struct sufs_fidx_entry)) % SUFS_PAGE_SIZE)


#define SUFS_DELE_REQUEST_READ         0
#define SUFS_DELE_REQUEST_WRITE        1
#define SUFS_DELE_REQUEST_KFS_CLEAR    2

#endif
