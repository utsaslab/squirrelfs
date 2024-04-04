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
#if 0
    char oldname[SUFS_NAME_MAX];
    char name[SUFS_NAME_MAX];

    struct sufs_libfs_mnode *olddir = NULL, *ms = NULL, *md = NULL, *cwd_m = NULL;

    cwd_m = proc->cwd_m;

    olddir = sufs_libfs_nameiparent(cwd_m, old_path, oldname);
    if (!olddir)
        return -1;

    /* Check if the old name exists; if not, abort right away */
    if (!(ms = sufs_libfs_mnode_dir_exists(olddir, oldname)))
        return -1;

    if (sufs_libfs_mnode_type(ms) == SUFS_MNODE_TYPE_DIR)
        return -1;

    md = sufs_libfs_nameiparent(cwd_m, new_path, name);
    if (!md)
        return -1;

    /*
     * Check if the target name already exists; if so,
     * no need to grab a link count on the old name.
     */
    if (sufs_libfs_mnode_dir_exists(md, name))
        return -1;

    if (!sufs_libfs_mnode_dir_insert(md, name, ms))
        return -1;

    return 0;
#endif
    return -1;
}

/* A special version for directory lookup used in rename */
static struct sufs_libfs_mnode*
sufs_libfs_rename_dir_lookup(struct sufs_libfs_mnode *mnode, char *name)
{
    unsigned long ino = 0;

    sufs_libfs_chainhash_lookup(&mnode->data.dir_data.map_, name, SUFS_NAME_MAX,
            &ino, NULL);

    return sufs_libfs_mnode_array[ino];
}

int sufs_libfs_sys_rename(struct sufs_libfs_proc *proc, char *old_path,
        char *new_path)
{
    char oldname[SUFS_NAME_MAX], newname[SUFS_NAME_MAX];
    struct sufs_libfs_mnode *cwd_m = NULL, *mdold = NULL, *mdnew = NULL,
            *mfold = NULL, *mfroadblock = NULL;

    struct sufs_libfs_ch_item * item = NULL;

    int ret = 0;

    int mfold_type = 0;

    cwd_m = proc->cwd_m;

    mdold = sufs_libfs_nameiparent(cwd_m, old_path, oldname);
    if (!mdold)
        return -1;

    mdnew = sufs_libfs_nameiparent(cwd_m, new_path, newname);
    if (!mdnew)
        return -1;

    if (strcmp(oldname, ".") == 0 || strcmp(oldname, "..") == 0
            || strcmp(newname, ".") == 0 || strcmp(newname, "..") == 0)
        return -1;


    sufs_libfs_file_enter_cs(mdold);

    if (sufs_libfs_map_file(mdold, 1) != 0)
        goto out_err_mdold;


    sufs_libfs_file_enter_cs(mdnew);

    if (sufs_libfs_map_file(mdnew, 1) != 0)
        goto out;

    if (!(mfold = sufs_libfs_rename_dir_lookup(mdold, oldname)))
        goto out;

    mfold_type = sufs_libfs_mnode_type(mfold);


    if (mdold == mdnew && oldname == newname)
        return 0;

    mfroadblock = sufs_libfs_rename_dir_lookup(mdnew, newname);

    if (mfroadblock)
    {
        int mfroadblock_type = sufs_libfs_mnode_type(mfroadblock);

        /*
         * Directories can be renamed to directories; and non-directories can
         * be renamed to non-directories. No other combinations are allowed.
         */

        if (mfroadblock_type != mfold_type)
            return -1;
    }

    if (mfroadblock == mfold)
    {
        /*
         * If the old and new paths point to the same inode, POSIX specifies
         * that we return successfully with no further action.
         */
        ret = 0;
        goto out;
    }

#if 0
    if (mdold != mdnew && mfold_type == SUFS_FILE_TYPE_DIR)
    {
        /* Loop avoidance: Abort if the source is
         * an ancestor of the destination. */

        struct sufs_libfs_mnode *md = mdnew;
        while (1)
        {
            if (mfold == md)
                return -1;
            if (md->mnum_ == sufs_root_mnum)
                break;

            md = sufs_libfs_mnode_dir_lookup(md, "..");
        }
    }
#endif


    /* Perform the actual rename operation in hash table */
    if (sufs_libfs_mnode_dir_replace_from(mdnew, newname, mfroadblock, mdold, oldname,
            mfold, mfold_type == SUFS_FILE_TYPE_DIR ? mfold : NULL, &item))
    {
        struct sufs_dir_entry * new_dir = NULL, * old_dir = NULL, * rb_dir = NULL;
        unsigned long journal_tail = 0;

        int name_len = strlen(newname) + 1;
        int cpu = 0;

        sufs_libfs_mnode_dir_entry_insert(mdnew, newname, name_len, mfold, &new_dir);
        memcpy(&(new_dir->inode), mfold->inode, sizeof(struct sufs_inode));

        old_dir = container_of(mfold->inode, struct sufs_dir_entry, inode);

        if (mfroadblock)
        {
            rb_dir = container_of(mfroadblock->inode, struct sufs_dir_entry,
                    inode);
        }

        /* File system state has not been changed till now */

        /*
         * journal locks need to be held when creating the journal and until
         * journal commits
         */

        cpu = sufs_libfs_current_cpu();

        pthread_spin_lock(&sufs_libfs_journal_locks[cpu]);

        journal_tail = sufs_libfs_create_rename_transaction(cpu,
                &(new_dir->name_len), &(old_dir->ino_num), &(rb_dir->ino_num));

        new_dir->name_len = name_len;
        sufs_libfs_clwb_buffer(new_dir, sizeof(struct sufs_dir_entry) + name_len);


        old_dir->ino_num = SUFS_INODE_TOMBSTONE;

        sufs_libfs_clwb_buffer(&(old_dir->ino_num), sizeof(old_dir->ino_num));

        if (mfroadblock)
        {
            rb_dir->ino_num = SUFS_INODE_TOMBSTONE;

            sufs_libfs_clwb_buffer(&(rb_dir->ino_num), sizeof(rb_dir->ino_num));
        }

        sufs_libfs_sfence();

        sufs_libfs_commit_lite_transaction(cpu, journal_tail);

        pthread_spin_unlock(&sufs_libfs_journal_locks[cpu]);

        item->val2 = (unsigned long) new_dir;

        mfold->inode = &(new_dir->inode);
        mfold->parent_mnum = mdnew->ino_num;

        ret = 0;
    }
    else
    {
        ret = -1;
    }


out:
    sufs_libfs_file_exit_cs(mdnew);

out_err_mdold:
    sufs_libfs_file_exit_cs(mdold);
    return ret;
}

static struct sufs_libfs_mnode* sufs_libfs_create(struct sufs_libfs_mnode *cwd,
        char *path, short type, unsigned int mode,
        unsigned int uid, unsigned int gid, bool excl, int * error)
{
    int inode = 0;

    char name[SUFS_NAME_MAX];
    struct sufs_libfs_mnode *md = sufs_libfs_nameiparent(cwd, path, name);
    struct sufs_libfs_mnode *mf = NULL;
    struct sufs_dir_entry * dir = NULL;
    int name_len = 0;

    if (!md || sufs_libfs_mnode_dir_killed(md))
    {
#if 0
        printf("Failed because md is %lx!\n", (unsigned long) md);
#endif
        return NULL;
    }

    if (excl && sufs_libfs_mnode_dir_exists(md, name))
    {
        if (error)
        {
            *error = EEXIST;
        }

        return NULL;
    }


    mf = sufs_libfs_mnode_dir_lookup(md, name);

    if (mf)
    {
        if (type != SUFS_FILE_TYPE_REG
                || !(sufs_libfs_mnode_type(mf) == SUFS_FILE_TYPE_REG)
                || excl)
            return NULL;

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

    if (sufs_libfs_mnode_dir_insert(md, name, name_len, mf, &dir))
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

    if ((ret = sufs_libfs_map_file(m, !(rwmode == O_RDONLY))) != 0)
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

    mf = sufs_libfs_mnode_dir_lookup(md, name);
    if (!mf)
        return -1;

    mf_type = sufs_libfs_mnode_type(mf);

    if (mf_type == SUFS_FILE_TYPE_DIR)
    {
        /*
         * Remove a subdirectory only if it has zero files in it.  No files
         * or sub-directories can be subsequently created in that directory.
         */
        if (!sufs_libfs_mnode_dir_kill(mf))
        {
            return -1;
        }
    }

    assert(sufs_libfs_mnode_dir_remove(md, name));

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

int sufs_libfs_sys_readdir(struct sufs_libfs_proc *proc, int dirfd,
        char *prevptr, char *nameptr)
{
    struct sufs_libfs_file_mnode *df = sufs_libfs_getfile(proc, dirfd);

    if (!df)
        return -1;

    if (sufs_libfs_mnode_type(df->m) != SUFS_FILE_TYPE_DIR)
        return -1;

    if (!sufs_libfs_mnode_dir_enumerate(df->m, prevptr, nameptr))
    {
        return 0;
    }

    return 1;
}

__ssize_t sufs_libfs_sys_getdents(struct sufs_libfs_proc *proc, int dirfd,
        void * buffer, size_t length)
{
    struct sufs_libfs_file_mnode *df = sufs_libfs_getfile(proc, dirfd);

    if (!df)
        return -1;

    if (sufs_libfs_mnode_type(df->m) != SUFS_FILE_TYPE_DIR)
        return -1;

    return sufs_libfs_mnode_dir_getdents(df->m, &(df->off), buffer, length);
}
