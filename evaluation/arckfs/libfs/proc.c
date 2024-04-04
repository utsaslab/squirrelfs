#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>


#include "../include/libfs_config.h"
#include "proc.h"
#include "mfs.h"
#include "filetable.h"

/*
 * TODO: We probably want to cache the sufs_proc in a per-thread variable
 * as Linux does.
 */

struct sufs_libfs_proc_list_entry *sufs_libfs_proc_list_head = NULL;

struct sufs_libfs_proc* sufs_libfs_proc_init(void)
{
    struct sufs_libfs_proc *ret = malloc(sizeof(struct sufs_libfs_proc));

    if (ret == NULL)
    {
        fprintf(stderr, "malloc sufs_proc failed!\n");
        return NULL;
    }

    ret->pid = getpid();

    ret->uid = getuid();
    ret->gid = getgid();

    /* FIXME: This should be acquired from the kernel */
    ret->cwd_m = sufs_libfs_root_dir;

    ret->ftable = malloc(sizeof(struct sufs_libfs_filetable));

    if (ret->ftable == NULL)
    {
        fprintf(stderr, "malloc sufs_filetable failed!\n");
        return NULL;
    }

    sufs_libfs_filetable_init(ret->ftable);

    return ret;
}

int sufs_libfs_proc_list_insert(struct sufs_libfs_proc *proc)
{
    struct sufs_libfs_proc_list_entry *entry = malloc(
            sizeof(struct sufs_libfs_proc_list_entry));

    if (entry == NULL)
    {
        fprintf(stderr, "Cannot allocate proc_list_entry!\n");
        return -ENOMEM;
    }

    entry->item = proc;
    entry->next = sufs_libfs_proc_list_head;

    sufs_libfs_proc_list_head = entry;

    return 0;
}

void sufs_libfs_proc_destroy(struct sufs_libfs_proc *proc)
{
    if (proc->ftable)
        free(proc->ftable);

    if (proc)
        free(proc);
}
