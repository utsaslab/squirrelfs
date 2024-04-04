#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <linux/limits.h>
#include <stdatomic.h>
#include <unistd.h>

#include "../include/libfs_config.h"
#include "util.h"
#include "syscall.h"
#include "orig_syscall.h"
#include "entry.h"
#include "super.h"
#include "mnode.h"
#include "mfs.h"
#include "cmd.h"
#include "ialloc.h"
#include "journal.h"
#include "simple_ring_buffer.h"
#include "tls.h"


int (*sufs_libfs_orig_open)(const char *, int, ...) = NULL;
int (*sufs_libfs_orig_openat)(int, const char *, int, ...) = NULL;

ssize_t (*sufs_libfs_orig_read)(int, void *, size_t) = NULL;
ssize_t (*sufs_libfs_orig_pread)(int, void *, size_t, off_t) = NULL;

ssize_t (*sufs_libfs_orig_write)(int, const void *, size_t) = NULL;
ssize_t (*sufs_libfs_orig_pwrite)(int, const void *, size_t, off_t) = NULL;

int (*sufs_libfs_orig_close)(int) = NULL;

int (*sufs_libfs_orig_xstat)(int, const char *, struct stat *) = NULL;
int (*sufs_libfs_orig_fxstat)(int, int, struct stat *) = NULL;
int (*sufs_libfs_orig_lxstat)(int, const char *, struct stat *) = NULL;

int (*sufs_libfs_orig_unlink)(const char *) = NULL;

off_t (*sufs_libfs_orig_lseek)(int , off_t, int) = NULL;

int (*sufs_libfs_orig_mkdir)(const char *, mode_t) = NULL;
int (*sufs_libfs_orig_mkdirat)(int, const char *, mode_t) = NULL;

int (*sufs_libfs_orig_chown)(const char *, uid_t, gid_t) = NULL;
int (*sufs_libfs_orig_chmod)(const char *, mode_t) = NULL;

int (*sufs_libfs_orig_rename)(const char *oldpath, const char *newpath) = NULL;
int (*sufs_libfs_orig_ftruncate)(int fd, off_t length) = NULL;

int (*sufs_libfs_orig_fsync)(int fd) = NULL;

__ssize_t
(*sufs_libfs_orig_getdents64) (int fd, void * buffer, size_t length) = NULL;


static void sufs_libfs_init_orig_function(void)
{
    /* Open the original function */
    /* TODO: error handling of dlsym */
    sufs_libfs_orig_open = dlsym(RTLD_NEXT, "open");
    assert(sufs_libfs_orig_open);

    sufs_libfs_orig_openat = dlsym(RTLD_NEXT, "openat");
    assert(sufs_libfs_orig_openat);

    sufs_libfs_orig_read = dlsym(RTLD_NEXT, "read");
    assert(sufs_libfs_orig_read);

    sufs_libfs_orig_pread = dlsym(RTLD_NEXT, "pread");
    assert(sufs_libfs_orig_pread);

    sufs_libfs_orig_write = dlsym(RTLD_NEXT, "write");
    assert(sufs_libfs_orig_write);

    sufs_libfs_orig_pwrite = dlsym(RTLD_NEXT, "pwrite");
    assert(sufs_libfs_orig_pwrite);

    sufs_libfs_orig_close = dlsym(RTLD_NEXT, "close");
    assert(sufs_libfs_orig_close);


    sufs_libfs_orig_xstat = dlsym(RTLD_NEXT, "__xstat");
    assert(sufs_libfs_orig_xstat);

    sufs_libfs_orig_fxstat = dlsym(RTLD_NEXT, "__fxstat");
    assert(sufs_libfs_orig_fxstat);

    sufs_libfs_orig_lxstat = dlsym(RTLD_NEXT, "__lxstat");
    assert(sufs_libfs_orig_lxstat);

    sufs_libfs_orig_unlink = dlsym(RTLD_NEXT, "unlink");
    assert(sufs_libfs_orig_unlink);

    sufs_libfs_orig_lseek = dlsym(RTLD_NEXT, "lseek");
    assert(sufs_libfs_orig_lseek);

    sufs_libfs_orig_mkdir = dlsym(RTLD_NEXT, "mkdir");
    assert(sufs_libfs_orig_mkdir);

    sufs_libfs_orig_mkdirat = dlsym(RTLD_NEXT, "mkdirat");
    assert(sufs_libfs_orig_mkdirat);

    sufs_libfs_orig_chown = dlsym(RTLD_NEXT, "chown");
    assert(sufs_libfs_orig_chown);

    sufs_libfs_orig_chmod = dlsym(RTLD_NEXT, "chown");
    assert(sufs_libfs_orig_chmod);

    sufs_libfs_orig_rename = dlsym(RTLD_NEXT, "rename");
    assert(sufs_libfs_orig_rename);

    sufs_libfs_orig_ftruncate = dlsym(RTLD_NEXT, "ftruncate");
    assert(sufs_libfs_orig_ftruncate);

    sufs_libfs_orig_fsync = dlsym(RTLD_NEXT, "fsync");
    assert(sufs_libfs_orig_fsync);

    sufs_libfs_orig_getdents64 = dlsym(RTLD_NEXT, "getdents64");
    assert(sufs_libfs_orig_getdents64);
}

int open(const char *pathname, int flags, ...)
{
    int mode = 0;
    int fd = 0;
    char * newpath = NULL;

    if ((flags & O_CREAT) != 0)
    {
        va_list arg;
        va_start (arg, flags);
        mode = va_arg(arg, int);
        va_end (arg);
    }

    newpath = sufs_libfs_upath_to_lib_path((char *) pathname);

    if (newpath == NULL)
        return sufs_libfs_orig_open(pathname, flags, mode);

    fd = sufs_libfs_sys_open(sufs_libfs_current_proc(), newpath, flags, mode);

    if (fd < 0)
    {
        return fd;
    }

     return sufs_libfs_lib_fd_to_ufd(fd);
}

int open64(const char *pathname, int flags, ...)
{
    int mode = 0;
    int fd = 0;
    char * newpath = NULL;

    if ((flags & O_CREAT) != 0)
    {
        va_list arg;
        va_start (arg, flags);
        mode = va_arg(arg, int);
        va_end (arg);
    }

    newpath = sufs_libfs_upath_to_lib_path((char *) pathname);

    if (newpath == NULL)
        return sufs_libfs_orig_open(pathname, flags, mode);

    fd = sufs_libfs_sys_open(sufs_libfs_current_proc(), newpath, flags, mode);

    if (fd < 0)
    {
        return fd;
    }

     return sufs_libfs_lib_fd_to_ufd(fd);
}

int openat(int dirfd, const char * pathname, int flags, ...)
{
    int mode = 0;
    int fd = 0;

    if ( (flags & O_CREAT) != 0)
    {
        va_list arg;
        va_start (arg, flags);
        mode = va_arg(arg, int);
        va_end (arg);
    }

    if (!sufs_libfs_is_lib_fd(dirfd))
        return sufs_libfs_orig_openat(dirfd, pathname, flags, mode);

    fd = sufs_libfs_sys_openat(sufs_libfs_current_proc(),
            sufs_libfs_ufd_to_lib_fd(dirfd), (char *) pathname, flags, mode);

    if (fd < 0)
    {
        return fd;
    }

    return sufs_libfs_lib_fd_to_ufd(fd);
}

int openat64(int dirfd, const char * pathname, int flags, ...)
{
    int mode = 0;
    int fd = 0;

    if ( (flags & O_CREAT) != 0)
    {
        va_list arg;
        va_start (arg, flags);
        mode = va_arg(arg, int);
        va_end (arg);
    }

    if (!sufs_libfs_is_lib_fd(dirfd))
        return sufs_libfs_orig_openat(dirfd, pathname, flags, mode);

    fd = sufs_libfs_sys_openat(sufs_libfs_current_proc(),
            sufs_libfs_ufd_to_lib_fd(dirfd), (char *) pathname, flags, mode);

    if (fd < 0)
    {
        return fd;
    }

    return sufs_libfs_lib_fd_to_ufd(fd);
}

int mkdir(const char *pathname, mode_t mode)
{
    char * newpath = sufs_libfs_upath_to_lib_path((char *) pathname);

    if (newpath == NULL)
        return sufs_libfs_orig_mkdir(pathname, mode);

    return sufs_libfs_sys_mkdir(sufs_libfs_current_proc(), newpath, mode);
}

int mkdirat(int dirfd, const char *pathname, mode_t mode)
{
    if (sufs_libfs_is_lib_fd(dirfd))
        return sufs_libfs_sys_mkdirat(sufs_libfs_current_proc(),
                sufs_libfs_ufd_to_lib_fd(dirfd), (char *) pathname, mode);
    else
        return sufs_libfs_orig_mkdirat(dirfd, pathname, mode);
}

off_t lseek(int fd, off_t offset, int whence)
{
    if (sufs_libfs_is_lib_fd(fd))
        return sufs_libfs_sys_lseek(sufs_libfs_current_proc(),
                sufs_libfs_ufd_to_lib_fd(fd), offset, whence);
    else
        return sufs_libfs_orig_lseek(fd, offset, whence);
}

off64_t lseek64(int fd, off64_t offset, int whence)
{
    return ((off_t) lseek(fd, (off_t) offset, whence));
}

int close(int fd)
{
    if (sufs_libfs_is_lib_fd(fd))
        return sufs_libfs_sys_close(sufs_libfs_current_proc(),
                sufs_libfs_ufd_to_lib_fd(fd));
    else
        return sufs_libfs_orig_close(fd);
}

int unlink(const char *path)
{
    char * newpath = sufs_libfs_upath_to_lib_path((char *) path);

    if (newpath == NULL)
        return sufs_libfs_orig_unlink(path);
    else
        return sufs_libfs_sys_unlink(sufs_libfs_current_proc(), newpath);
}

int fstat(int fd, struct stat *statbuf)
{
    if (sufs_libfs_is_lib_fd(fd))
        return sufs_libfs_sys_fstat(sufs_libfs_current_proc(),
                sufs_libfs_ufd_to_lib_fd(fd), statbuf);
    else
        return sufs_libfs_orig_fxstat(3, fd, statbuf);
}

int __fxstat(int ver, int fd, struct stat *statbuf)
{
    if (sufs_libfs_is_lib_fd(fd))
        return sufs_libfs_sys_fstat(sufs_libfs_current_proc(),
                sufs_libfs_ufd_to_lib_fd(fd), statbuf);
    else
        return sufs_libfs_orig_fxstat(ver, fd, statbuf);
}


int __fxstat64(int ver, int fd, struct stat64 *statbuf)
{
    return __fxstat(ver, fd, (struct stat *) statbuf);
}

int lstat(const char *pathname, struct stat *statbuf)
{
    char * newpath = sufs_libfs_upath_to_lib_path((char *) pathname);

    if (newpath == NULL)
        return sufs_libfs_orig_lxstat(3, pathname, statbuf);
    else
        return sufs_libfs_sys_lstat(sufs_libfs_current_proc(), newpath, statbuf);
}

int __lxstat(int ver, const char *pathname, struct stat *statbuf)
{
    char * newpath = sufs_libfs_upath_to_lib_path((char *) pathname);

    if (newpath == NULL)
        return sufs_libfs_orig_lxstat(ver, pathname, statbuf);
    else
        return sufs_libfs_sys_lstat(sufs_libfs_current_proc(), newpath, statbuf);
}


int __lxstat64(int ver, const char *pathname, struct stat64 *statbuf)
{
    return __lxstat(ver, pathname, (struct stat *) statbuf);
}

/* FIXME: Here we just use lstat for stat */
int stat(const char *pathname, struct stat *statbuf)
{
    char * newpath = sufs_libfs_upath_to_lib_path((char *) pathname);

    if (newpath == NULL)
        return sufs_libfs_orig_lxstat(3, pathname, statbuf);
    else
        return sufs_libfs_sys_lstat(sufs_libfs_current_proc(), newpath, statbuf);
}

int __xstat(int ver, const char *pathname, struct stat *statbuf)
{
    char * newpath = sufs_libfs_upath_to_lib_path((char *) pathname);

    if (newpath == NULL)
        return sufs_libfs_orig_xstat(ver, pathname, statbuf);
    else
        return sufs_libfs_sys_lstat(sufs_libfs_current_proc(), newpath, statbuf);
}

int __xstat64(int ver, const char *pathname, struct stat64 *statbuf)
{
    return __xstat(ver, pathname, (struct stat *) statbuf);
}

ssize_t read(int fd, void * buf, size_t count)
{
    if (sufs_libfs_is_lib_fd(fd))
        return sufs_libfs_sys_read(sufs_libfs_current_proc(),
                sufs_libfs_ufd_to_lib_fd(fd), buf, count);
    else
        return sufs_libfs_orig_read(fd, buf, count);
}

ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
    if (sufs_libfs_is_lib_fd(fd))
        return sufs_libfs_sys_pread(sufs_libfs_current_proc(),
                sufs_libfs_ufd_to_lib_fd(fd), buf, count, offset);
    else
        return sufs_libfs_orig_pread(fd, buf, count, offset);
}

ssize_t write(int fd, const void * buf, size_t count)
{
    if (sufs_libfs_is_lib_fd(fd))
        return sufs_libfs_sys_write(sufs_libfs_current_proc(),
                sufs_libfs_ufd_to_lib_fd(fd), (void *) buf, count);
    else
        return sufs_libfs_orig_write(fd, buf, count);
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    if (sufs_libfs_is_lib_fd(fd))
        return sufs_libfs_sys_pwrite(sufs_libfs_current_proc(),
                sufs_libfs_ufd_to_lib_fd(fd), (void *) buf, count, offset);
    else
        return sufs_libfs_orig_pwrite(fd, buf, count, offset);
}

unsigned long sufs_get(char * path, void * buf, unsigned long count)
{
    char * newpath = sufs_libfs_upath_to_lib_path(path);

    if (newpath == NULL)
    {
        abort();
    }

    return sufs_libfs_get(sufs_libfs_current_proc(),
            newpath, buf, count);
}

unsigned long sufs_set(char * path, void * buf, unsigned long count)
{
    char * newpath = sufs_libfs_upath_to_lib_path(path);

    if (newpath == NULL)
    {
        abort();
    }

    return sufs_libfs_set(sufs_libfs_current_proc(),
            newpath, buf, count);
}

int chown(const char * path, uid_t owner, gid_t group)
{
    char * newpath = sufs_libfs_upath_to_lib_path((char *) path);

    if (newpath == NULL)
        return sufs_libfs_orig_chown(path, owner, group);
    else
        return sufs_libfs_sys_chown(sufs_libfs_current_proc(),
                newpath, owner, group);
}

int chmod(const char *path, mode_t mode)
{
    char * newpath = sufs_libfs_upath_to_lib_path((char *) path);

    if (newpath == NULL)
        return sufs_libfs_orig_chmod(path, mode);
    else
        return sufs_libfs_sys_chmod(sufs_libfs_current_proc(),
                newpath, mode);
}

int sufs_sys_readdir(int fd, char * prev_name, char * this_name)
{
    return sufs_libfs_sys_readdir(sufs_libfs_current_proc(),
            sufs_libfs_ufd_to_lib_fd(fd), prev_name, this_name);
}


int rename(const char * oldpath, const char * newpath)
{
    char * oldpath_n = sufs_libfs_upath_to_lib_path((char *) oldpath);
    char * newpath_n = sufs_libfs_upath_to_lib_path((char *) newpath);

    if (oldpath_n == NULL || newpath_n == NULL)
        return sufs_libfs_orig_rename(oldpath, newpath);
    else
        return sufs_libfs_sys_rename(sufs_libfs_current_proc(), oldpath_n,
                newpath_n);
}

int ftruncate(int fd, off_t length)
{
    if (sufs_libfs_is_lib_fd(fd))
        return sufs_libfs_sys_ftruncate(sufs_libfs_current_proc(),
                sufs_libfs_ufd_to_lib_fd(fd), length);
    else
        return sufs_libfs_orig_ftruncate(fd, length);
}

int ftruncate64(int fd, off64_t length)
{
    return ftruncate(fd, (off_t) length);
}

int fsync(int fd)
{
    if (sufs_libfs_is_lib_fd(fd))
        return 0;
    else
        return sufs_libfs_orig_fsync(fd);
}

__ssize_t getdents64 (int fd, void * buffer, size_t length)
{
    if (sufs_libfs_is_lib_fd(fd))
        return sufs_libfs_sys_getdents(sufs_libfs_current_proc(),
                sufs_libfs_ufd_to_lib_fd(fd), buffer, length);
    else
        return sufs_libfs_orig_getdents64(fd, buffer, length);
}



#if 0
int chdir(const char *path)
{
    return sys_chdir(sufs_libfs_current_proc(), path);
}
#endif


static void sufs_libfs_do_proc_init(void)
{
    struct sufs_libfs_proc *proc = sufs_libfs_proc_init();

    if (!proc)
    {
        fprintf(stderr, "Cannot init proc!\n");
        abort();
    }

    if (sufs_libfs_proc_list_insert(proc) != 0)
    {
        fprintf(stderr, "Cannot insert proc!\n");
        abort();
    }

    return;
}


static void sufs_libfs_do_proc_exit(void)
{
    struct sufs_libfs_proc_list_entry *iter = sufs_libfs_proc_list_head;

    if (iter)
    {
        sufs_libfs_proc_destroy(iter->item);
        sufs_libfs_proc_list_delete(NULL, iter);
    }

    return;
}


static char * sufs_libfs_premap_str = NULL;

static void sufs_libfs_handle_env(void)
{
    char * ptr = NULL;

    ptr = getenv("sufs_alloc_cpu");

    if (ptr)
    {
        sufs_libfs_alloc_cpu = atoi(ptr);
    }

    ptr = getenv("sufs_alloc_numa");

    if (ptr)
    {
        sufs_libfs_alloc_numa = atoi(ptr);
    }

    ptr = getenv("sufs_init_alloc_size");

    if (ptr)
    {
        sufs_libfs_init_alloc_size = atol(ptr);
    }

    ptr = getenv("sufs_alloc_pin_cpu");

    if (ptr)
    {
        sufs_alloc_pin_cpu = atoi(ptr);
    }

    ptr = getenv("sufs_preload_file");

    if (ptr)
    {
        sufs_libfs_premap_str = strdup(ptr);
#if 0
        printf("preload string = %s\n", sufs_libfs_premap_str);
#endif
    }
}

static void sufs_libfs_premap_files(char * str)
{
    char * file_str = NULL;
    int fd = 0, cpu = 0;

    while ((file_str = strsep(&str, ",")) != NULL)
    {
        /*
        sufs_libfs_pin_to_core(cpu);
        cpu++;
        */

        fd = open(file_str, O_RDWR);

        if (fd > 0)
        {
#if 0
        printf("premap file: %s\n", file_str);
#endif
            close(fd);
        }
    }
}

__attribute__ ((constructor))
void sufs_libfs_init(void)
{
    sufs_libfs_handle_env();

    sufs_libfs_init_orig_function();

    sufs_libfs_cmd_init();

    sufs_libfs_tls_init();

    sufs_libfs_init_super_block(&sufs_libfs_sb);

    sufs_libfs_mnodes_init();

    sufs_libfs_mfs_init();

    sufs_libfs_fs_init();

    /* This needs to be performed after we have registered the LibFS with KFS */
    sufs_libfs_alloc_inode_free_lists(&sufs_libfs_sb);

    sufs_libfs_init_inode_free_lists(&sufs_libfs_sb);

    sufs_libfs_alloc_block_free_lists(&sufs_libfs_sb);

    sufs_libfs_init_block_free_list(&sufs_libfs_sb, 0);

    /* This needs to be performed after we have initialized the root */
    sufs_libfs_do_proc_init();

    sufs_libfs_lite_journal_hard_init(&sufs_libfs_sb);

    sufs_libfs_ring_buffer_connect(&sufs_libfs_sb);

    sufs_libfs_premap_files(sufs_libfs_premap_str);

#if 0
    {
        int cpu = 0, node = 0;
        sufs_libfs_getcpu(&cpu, &node);

        printf("cpu is %d, node is %d\n", cpu, node);
    }
#endif
}

__attribute__((destructor))
void sufs_libfs_fini(void)
{
#if SUFS_LIBFS_STAT
    sufs_libfs_print_timing_stats();
#endif

    sufs_libfs_do_proc_exit();

    sufs_libfs_free_inode_free_lists(&sufs_libfs_sb);

    sufs_libfs_delete_block_free_lists(&sufs_libfs_sb);

    sufs_libfs_fs_fini();

    sufs_libfs_mnodes_fini();

    sufs_libfs_mfs_fini();

    sufs_libfs_cmd_fini();
}



