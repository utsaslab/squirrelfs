#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdatomic.h>

#include "../include/libfs_config.h"
#include "filetable.h"
#include "mfs.h"
#include "amd64.h"
#include "atomic_util.h"
#include "util.h"

struct sufs_libfs_fdinfo sufs_libfs_filetable_lock_fdinfo(
        struct sufs_libfs_fdinfo *infop)
{
    struct sufs_libfs_fdinfo info;
    while (true)
    {
        info.data_ = __atomic_load_n(&(infop->data_), __ATOMIC_RELAXED);
retry:
        if (sufs_libfs_fdinfo_get_locked(&info))
            sufs_libfs_nop_pause();
        else
            break;
    }

    if (!__atomic_compare_exchange_n(&(infop->data_), &(info.data_),
            sufs_libfs_fdinfo_with_locked(&info, true).data_, 1,
            __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        goto retry;

    return info;
}

struct sufs_libfs_file_mnode*
sufs_libfs_filetable_getfile(struct sufs_libfs_filetable *ft, int fd)
{
    void *f = NULL;
    int cpu = fd >> SUFS_LIBFS_FILETABLE_CPUSHIFT;

    fd = fd & SUFS_LIBFS_FILETABLE_FDMASK;

    if (cpu < 0 || cpu >= SUFS_MAX_CPU)
        return NULL;

    if (fd < 0 || fd >= SUFS_LIBFS_MAX_FD)
        return NULL;

    /*
     * XXX This isn't safe: there could be a concurrent close that
     * drops the reference count to zero.
     */

    f = sufs_libfs_fdinfo_getfile(&ft->info_[cpu][fd]);

    return f;
}

/*
 * Allocate a FD and point it to f.  This takes over the reference
 * to f from the caller.
 */
int sufs_libfs_filetable_allocfd(struct sufs_libfs_filetable *ft,
        struct sufs_libfs_file_mnode *f,
        bool percpu, bool cloexec)
{
    int fd = 0;
    int cpu = percpu ? sufs_libfs_current_cpu() : 0;

    struct sufs_libfs_fdinfo none, newinfo;

    sufs_libfs_fdinfo_init(&none, NULL, false, false);
    sufs_libfs_fdinfo_init(&newinfo, f, cloexec, true);

    for (fd = 0; fd < SUFS_LIBFS_MAX_FD; fd++)
    {
        /*
         * Note that we skip over locked FDs because that means they're
         * either non-null or about to be.
         */

        if (__atomic_load_n(&(ft->info_[cpu][fd].data_), __ATOMIC_RELAXED)
                == none.data_
                && sufs_libfs_cmpxch_fdinfo(&ft->info_[cpu][fd], none, newinfo))
        {
            /*
             * The default state of cloexec_ is 'true', so we only need to
             * write to it if this is a keep-exec FD.
             */

            if (!cloexec)
                ft->cloexec_[cpu][fd] = cloexec;

            /* Unlock FD */
            __atomic_store_n(&(ft->info_[cpu][fd].data_),
                    sufs_libfs_fdinfo_with_locked(&newinfo, false).data_,
                    __ATOMIC_RELEASE);

            return (cpu << SUFS_LIBFS_FILETABLE_CPUSHIFT) | fd;
        }
    }
    fprintf(stderr, "filetable::allocfd: failed\n");
    return -1;
}

void sufs_libfs_filetable_close(struct sufs_libfs_filetable *ft, int fd)
{
    /*
     * XXX(sbw) if f->ref_ > 1 the kernel will not actually close
     * the file when this function returns (i.e. sys_close can return
     * while the file/pipe/socket is still open).
     */

    int cpu = fd >> SUFS_LIBFS_FILETABLE_CPUSHIFT;
    fd = fd & SUFS_LIBFS_FILETABLE_FDMASK;

    struct sufs_libfs_fdinfo *infop, fdinfo, newinfo;

    if (cpu < 0 || cpu >= SUFS_MAX_CPU)
    {
        fprintf(stderr, "filetable::close: bad fd cpu %u\n", cpu);
        return;
    }

    if (fd < 0 || fd >= SUFS_LIBFS_MAX_FD)
    {
        fprintf(stderr, "filetable::close: bad fd %u\n", fd);
        return;
    }

    /* Lock the FD to prevent concurrent modifications */
    infop = &ft->info_[cpu][fd];
    fdinfo = sufs_libfs_filetable_lock_fdinfo(infop);

    /* Clear cloexec_ back to default state of 'true' */
    if (!ft->cloexec_[cpu][fd])
        ft->cloexec_[cpu][fd] = true;

    sufs_libfs_fdinfo_init(&newinfo, NULL, false, false);

    /* Update and unlock the FD */
    __atomic_store_n(&(infop->data_), newinfo.data_, __ATOMIC_RELEASE);
}

void sufs_libfs_filetable_init(struct sufs_libfs_filetable *ft)
{
    struct sufs_libfs_fdinfo none;
    int cpu = 0, fd = 0;

    sufs_libfs_fdinfo_init(&none, NULL, false, false);

    for (cpu = 0; cpu < SUFS_MAX_CPU; cpu++)
    {
        for (fd = 0; fd < SUFS_LIBFS_MAX_FD; fd++)
        {
            __atomic_store_n(&ft->info_[cpu][fd].data_, none.data_,
            __ATOMIC_RELAXED);

            __atomic_store_n(&ft->cloexec_[cpu][fd], true,
            __ATOMIC_RELAXED);
        }
    }

    __atomic_thread_fence(__ATOMIC_RELAXED);
}
