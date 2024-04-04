#ifndef SUFS_LIBFS_SYSCALL_H_
#define SUFS_LIBFS_SYSCALL_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <dirent.h>

#include "../../include/libfs_config.h"
#include "proc.h"

struct linux_dirent64
{
   unsigned long   d_ino;    /* 64-bit inode number */
   unsigned long   d_off;    /* 64-bit offset to next structure */
   unsigned short d_reclen; /* Size of this dirent */
   unsigned char  d_type;   /* File type */
   char           d_name[]; /* Filename (null-terminated) */
};

__ssize_t getdents64 (int fd, void * buffer, size_t length);

off_t sufs_libfs_sys_lseek(struct sufs_libfs_proc *proc, int fd, off_t offset,
        int whence);
int sufs_libfs_sys_fstat(struct sufs_libfs_proc *proc, int fd,
        struct stat *stat);
int sufs_libfs_sys_lstat(struct sufs_libfs_proc *proc, char *path,
        struct stat *stat);

int sufs_libfs_sys_close(struct sufs_libfs_proc *proc, int fd);
int sufs_libfs_sys_link(struct sufs_libfs_proc *proc, char *old_path,
        char *new_path);
int sufs_libfs_sys_rename(struct sufs_libfs_proc *proc, char *old_path,
        char *new_path);
int sufs_libfs_sys_openat(struct sufs_libfs_proc *proc, int dirfd, char *path,
        int flags, int mode);
int sufs_libfs_sys_unlink(struct sufs_libfs_proc *proc, char *path);

ssize_t sufs_libfs_sys_read(struct sufs_libfs_proc *proc, int fd, void *p,
        size_t n);
ssize_t sufs_libfs_sys_pread(struct sufs_libfs_proc *proc, int fd, void *ubuf,
        size_t count, off_t offset);
ssize_t sufs_libfs_sys_pwrite(struct sufs_libfs_proc *proc, int fd, void *ubuf,
        size_t count, off_t offset);
ssize_t sufs_libfs_sys_write(struct sufs_libfs_proc *proc, int fd, void *p,
        size_t n);

int sufs_libfs_sys_mkdirat(struct sufs_libfs_proc *proc, int dirfd, char *path,
        mode_t mode);
int sufs_libfs_sys_chdir(struct sufs_libfs_proc *proc, char *path);
int sufs_libfs_sys_readdir(struct sufs_libfs_proc *proc, int dirfd,
        char *prevptr, char *nameptr);

int sufs_libfs_sys_chown(struct sufs_libfs_proc *proc, char * path,
        uid_t owner, gid_t group);

int sufs_libfs_sys_chmod(struct sufs_libfs_proc *proc, char * path,
        mode_t mode);

int sufs_libfs_sys_ftruncate(struct sufs_libfs_proc *proc, int fd,
        off_t length);

__ssize_t sufs_libfs_sys_getdents(struct sufs_libfs_proc *proc, int dirfd,
        void * buffer, size_t length);

static inline int sufs_libfs_sys_mkdir(struct sufs_libfs_proc *proc, char *path,
        mode_t mode)
{
    return sufs_libfs_sys_mkdirat(proc, AT_FDCWD, path, mode);
}

static inline int sufs_libfs_sys_open(struct sufs_libfs_proc *proc, char *path,
        int flags, int mode)
{
    return sufs_libfs_sys_openat(proc, AT_FDCWD, path, flags, mode);
}

#endif /* SUFS_SYSCALL_H_ */
