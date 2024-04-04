#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>

#include "../include/libfs_config.h"
#include "../include/common_inode.h"
#include "syscall.h"
#include "mnode.h"
#include "filetable.h"
#include "proc.h"
#include "mfs.h"
#include "file.h"
#include "ialloc.h"
#include "util.h"
#include "cmd.h"
#include "journal.h"

static struct sufs_libfs_file_mnode* sufs_libfs_getfile(struct sufs_libfs_proc
        *proc, int fd)
{
    struct sufs_libfs_filetable *filetable = proc->ftable;
    return sufs_libfs_filetable_getfile(filetable, fd);
}

/*
 * Allocate a file descriptor for the given file.
 * Takes over file reference from caller on success.
 */
static int sufs_libfs_fdalloc(struct sufs_libfs_proc *proc,
        struct sufs_libfs_file_mnode *f, int omode)
{
    struct sufs_libfs_filetable *filetable = proc->ftable;
    if (!f)
        return -1;

    return sufs_libfs_filetable_allocfd(filetable, f, omode & O_ANYFD,
            omode & O_CLOEXEC);
}

/* How to handle concurrent access to the same file descriptor */
off_t sufs_libfs_sys_lseek(struct sufs_libfs_proc *proc, int fd,
        off_t offset, int whence)
{
    struct sufs_libfs_file_mnode *f = sufs_libfs_getfile(proc, fd);

    if (!f)
        return -1;

    if (sufs_libfs_mnode_type(f->m) != SUFS_FILE_TYPE_REG)
        return -1; //ESPIPE

    if (whence == SEEK_CUR)
    {
        offset += f->off;
    }
    else if (whence == SEEK_END)
    {
        u64 size = sufs_libfs_mnode_file_size(f->m);
        offset += size;
    }

    if (offset < 0)
        return -1;

    f->off = offset;

    return offset;
}

int sufs_libfs_sys_fstat(struct sufs_libfs_proc *proc, int fd,
        struct stat *stat)
{
    struct sufs_libfs_file_mnode *f = sufs_libfs_getfile(proc, fd);
    if (!f)
        return -1;

    if (sufs_libfs_mnode_stat(f->m, stat) < 0)
        return -1;

    return 0;
}

int sufs_libfs_sys_lstat(struct sufs_libfs_proc *proc, char *path,
        struct stat *stat)
{
    struct sufs_libfs_mnode *m = NULL;

    m = sufs_libfs_namei(proc->cwd_m, path);
    if (!m)
    {
        errno = ENOENT;
        return -1;
    }

    if (sufs_libfs_mnode_stat(m, stat) < 0)
        return -1;

    return 0;
}

int sufs_libfs_sys_close(struct sufs_libfs_proc *proc, int fd)
{
    struct sufs_libfs_file_mnode *f = sufs_libfs_getfile(proc, fd);
    if (!f)
        return -1;

    /* Informs the kernel that this file is no longer in the critical section */
    sufs_libfs_file_exit_cs(f->m);

    sufs_libfs_filetable_close(proc->ftable, fd);

    return 0;
}

int sufs_libfs_sys_link(struct sufs_libfs_proc *proc, char *old_path, char *new_path)
{
    return -1;
}

static struct sufs_libfs_mnode* sufs_libfs_create(struct sufs_libfs_mnode *cwd,
        char *path, short type, unsigned int mode,
        unsigned int uid, unsigned int gid, bool excl, int * error)
{
    int inode = 0;

    char name[SUFS_NAME_MAX];
    struct sufs_libfs_mnode *md = NULL;
    struct sufs_libfs_mnode *mf = NULL;
    struct sufs_dir_entry * dir = NULL;
    int name_len = 0;

    md = sufs_libfs_nameiparent(cwd, path, name);

    if (!md || sufs_libfs_mnode_dir_killed(md))
    {
#if 0
        printf("Failed because md is %lx!\n", (unsigned long) md);
#endif
        return NULL;
    }

    if (excl && sufs_libfs_mnode_dir_exists(md, path))
    {
        if (error)
        {
            *error = EEXIST;
        }

#if 0
        printf("Failed because md exists!\n");
#endif
        return NULL;
    }

    mf = sufs_libfs_mnode_dir_lookup(md, path);

    if (mf)
    {
        if (type != SUFS_FILE_TYPE_REG
                || !(sufs_libfs_mnode_type(mf) == SUFS_FILE_TYPE_REG)
                || excl)
        {
#if 0
        printf("Failed because flags!\n");
#endif
            return NULL;
        }

        return mf;
    }

    inode = sufs_libfs_new_inode(&sufs_libfs_sb, sufs_libfs_current_cpu());

    if (inode <= 0)
    {
        if (error)
        {
            *error = ENOSPC;
        }

        return NULL;
    }

    mf = sufs_libfs_mfs_mnode_init(type, inode, md->ino_num, NULL);

    name_len = strlen(name) + 1;

    if (sufs_libfs_mnode_dir_insert(md, name, name_len, path, mf, &dir))
    {
        sufs_libfs_inode_init(&(dir->inode), type, mode, uid, gid, 0);

        mf->inode = &(dir->inode);
        mf->index_start = NULL;
        mf->index_end = NULL;

        /* update name_len here to finish the creation */
        dir->name_len = name_len;

        sufs_libfs_clwb_buffer(dir, sizeof(struct sufs_dir_entry) + name_len);
        sufs_libfs_sfence();

        return mf;
    }

#if 0
        printf("Failed during insert!\n");
#endif

    sufs_libfs_free_inode(&sufs_libfs_sb, inode);
    sufs_libfs_mnode_array[inode] = NULL;

    free(mf);

    return NULL;
}

int sufs_libfs_sys_openat(struct sufs_libfs_proc *proc, int dirfd, char *path,
        int flags, int mode)
{
    struct sufs_libfs_mnode *cwd = NULL, *m = NULL;
    struct sufs_libfs_file_mnode *f = NULL;
    int rwmode = 0;
    int ret = 0;
    int err = 0;

    if (dirfd == AT_FDCWD)
    {
        cwd = proc->cwd_m;
    }
    else
    {
        struct sufs_libfs_file_mnode *fdirm =
                (struct sufs_libfs_file_mnode*) sufs_libfs_getfile(proc, dirfd);

        if (!fdirm)
            return -1;

        cwd = fdirm->m;
    }

    if (flags & O_CREAT)
        m = sufs_libfs_create(cwd, path, SUFS_FILE_TYPE_REG,
                mode, proc->uid, proc->gid, flags & O_EXCL, &err);
    else
        m = sufs_libfs_namei(cwd, path);

    if (!m)
    {
        errno = err;
        return -1;
    }

    rwmode = flags & (O_RDONLY | O_WRONLY | O_RDWR);
    if ((sufs_libfs_mnode_type(m) == SUFS_FILE_TYPE_DIR) && (rwmode != O_RDONLY))
        return -1;

#if 0
    printf("mnode %d map: %d\n", m->ino_num, sufs_libfs_file_is_mapped(m));
#endif

    if ((ret = sufs_libfs_map_file(m, !(rwmode == O_RDONLY), path)) != 0)
        return ret;

    /* release it during close */
    sufs_libfs_file_enter_cs(m);

    if ((sufs_libfs_mnode_type(m) == SUFS_FILE_TYPE_REG) && (flags & O_TRUNC))
    {
        if (sufs_libfs_mnode_file_size(m))
            sufs_libfs_mnode_file_truncate_zero(m);
    }

    f = sufs_libfs_file_mnode_init(m, !(rwmode == O_WRONLY), !(rwmode == O_RDONLY),
            !!(flags & O_APPEND));

    return sufs_libfs_fdalloc(proc, f, flags);
}

int sufs_libfs_sys_unlink(struct sufs_libfs_proc *proc, char *path)
{
    char name[SUFS_NAME_MAX];
    struct sufs_libfs_mnode *md = NULL, *cwd_m = NULL, *mf = NULL;
    int mf_type = 0;

    cwd_m = proc->cwd_m;

    md = sufs_libfs_nameiparent(cwd_m, path, name);
    if (!md)
        return -1;

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return -1;

    mf = sufs_libfs_mnode_dir_lookup(md, path);
    if (!mf)
        return -1;

    mf_type = sufs_libfs_mnode_type(mf);

    if (mf_type == SUFS_FILE_TYPE_DIR)
    {

    }

    assert(sufs_libfs_mnode_dir_remove(md, path));

    sufs_libfs_mnode_file_delete(mf);

    if (sufs_libfs_is_inode_allocated(mf->ino_num))
    {
        sufs_libfs_free_inode(&sufs_libfs_sb, mf->ino_num);
    }


    sufs_libfs_mnode_array[mf->ino_num] = NULL;

    sufs_libfs_mnode_free(mf);

    return 0;
}

ssize_t sufs_libfs_sys_read(struct sufs_libfs_proc *proc, int fd, void *p, size_t n)
{
    struct sufs_libfs_file_mnode *f = NULL;
    ssize_t res = 0;

    f = sufs_libfs_getfile(proc, fd);

    if (!f)
    {
        fprintf(stderr, "Cannot find file from fd: %d\n", fd);
        return -1;
    }

    res = sufs_libfs_file_mnode_read(f, (char*) p, n);
    if (res < 0)
    {
        res = -1;
        goto out;
    }

out:
    return res;
}

ssize_t sufs_libfs_sys_pread(struct sufs_libfs_proc *proc, int fd, void *ubuf,
        size_t count, off_t offset)
{
    struct sufs_libfs_file_mnode *f = NULL;
    ssize_t r = 0;

    f = sufs_libfs_getfile(proc, fd);

    if (!f)
    {
        fprintf(stderr, "Cannot find file from fd: %d\n", fd);
        return -1;
    }

    r = sufs_libfs_file_mnode_pread(f, (char*) ubuf, count, offset);

    return r;
}

ssize_t sufs_libfs_sys_pwrite(struct sufs_libfs_proc *proc, int fd,
        void *ubuf, size_t count, off_t offset)
{
    struct sufs_libfs_file_mnode *f = NULL;
    ssize_t r = 0;

    f = sufs_libfs_getfile(proc, fd);

    if (!f)
    {
        fprintf(stderr, "Cannot find file from fd: %d\n", fd);
        return -1;
    }

    r = sufs_libfs_file_mnode_pwrite(f, (char*) ubuf, count, offset);

    return r;
}

ssize_t sufs_libfs_sys_write(struct sufs_libfs_proc *proc, int fd, void *p,
        size_t n)
{
    struct sufs_libfs_file_mnode *f = NULL;

    ssize_t res = 0;

    f = sufs_libfs_getfile(proc, fd);

    if (!f)
    {
        fprintf(stderr, "Cannot find file from fd: %d\n", fd);
        return -1;
    }

    res = sufs_libfs_file_mnode_write(f, (char*) p, n);

    return res;
}

int sufs_libfs_sys_mkdirat(struct sufs_libfs_proc *proc, int dirfd, char *path,
        mode_t mode)
{
    struct sufs_libfs_mnode *cwd = NULL;
    int err = 0;

    if (strcmp(path, "/") == 0)
    {
        errno = EEXIST;
        return -1;
    }

    if (dirfd == AT_FDCWD)
    {
        cwd = proc->cwd_m;
    }
    else
    {
        struct sufs_libfs_file_mnode *fdir = sufs_libfs_getfile(proc, dirfd);
        if (!fdir)
            return -1;

        cwd = fdir->m;
    }

    if (!sufs_libfs_create(cwd, path, SUFS_FILE_TYPE_DIR, mode,
            proc->uid, proc->gid, true, &err))
    {
#if 0
        printf("failed at sufs_libfs_create!\n");
#endif
        errno = err;
        return -1;
    }

    return 0;
}

int sufs_libfs_sys_chown(struct sufs_libfs_proc *proc, char * path,
        uid_t owner, gid_t group)
{
    struct sufs_libfs_mnode *m = NULL;
    unsigned long inode_offset = 0;

    m = sufs_libfs_namei(proc->cwd_m, path);
    if (!m)
        return -1;

    inode_offset = sufs_libfs_virt_addr_to_offset((unsigned long) m->inode);

    return sufs_libfs_cmd_chown(m->ino_num, owner, group, inode_offset);
}

int sufs_libfs_sys_chmod(struct sufs_libfs_proc *proc, char * path,
        mode_t mode)
{
    struct sufs_libfs_mnode *m = NULL;
    unsigned long inode_offset = 0;

    m = sufs_libfs_namei(proc->cwd_m, path);
    if (!m)
        return -1;

    inode_offset = sufs_libfs_virt_addr_to_offset((unsigned long) m->inode);

    return sufs_libfs_cmd_chmod(m->ino_num, mode, inode_offset);
}

int sufs_libfs_sys_ftruncate(struct sufs_libfs_proc *proc, int fd,
        off_t length)
{
    struct sufs_libfs_file_mnode *f = NULL;

    ssize_t res = 0;

    f = sufs_libfs_getfile(proc, fd);

    if (!f)
    {
        fprintf(stderr, "Cannot find file from fd: %d\n", fd);
        return -1;
    }

    res = sufs_libfs_file_mnode_truncate(f, length);

    return res;
}



int sufs_libfs_sys_chdir(struct sufs_libfs_proc *proc, char *path)
{
#if 0
    struct sufs_libfs_mnode *m = NULL;

    m = sufs_libfs_namei(proc->cwd_m, path);
    if (!m || sufs_libfs_mnode_type(m) != SUFS_MNODE_TYPE_DIR)
        return -1;

    proc->cwd_m = m;

    return 0;
#endif
    return -1;
}


