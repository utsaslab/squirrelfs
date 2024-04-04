#ifndef SUFS_LIBFS_ENTRY_H_
#define SUFS_LIBFS_ENTRY_H_

#include "../../include/libfs_config.h"

static inline int sufs_libfs_is_lib_fd(int fd)
{
    return (fd >= SUFS_LIBFS_BASE_FD);
}

static inline int sufs_libfs_ufd_to_lib_fd(int ufd)
{
    return (ufd - SUFS_LIBFS_BASE_FD);
}

static inline int sufs_libfs_lib_fd_to_ufd(int libfd)
{
    return (libfd + SUFS_LIBFS_BASE_FD);
}

/* check whether the path starts with SUFS_ROOT_PATH or not */
static inline char * sufs_libfs_upath_to_lib_path(char * path)
{
    char * root = SUFS_ROOT_PATH;

    /* whether string path starts with string root */
    while (*path == *root)
    {
        if (*path == 0 || *root == 0)
            break;

        path++;
        root++;
    }

    if (*root == 0)
    {
        return (path - 1);
    }
    else
    {
        return NULL;
    }
}

#endif
