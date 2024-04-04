#ifndef SUFS_LIBFS_PROC_H_
#define SUFS_LIBFS_PROC_H_

#include "../../include/libfs_config.h"
#include "mnode.h"
#include "filetable.h"

struct sufs_libfs_proc_list_entry
{
        struct sufs_libfs_proc *item;
        struct sufs_libfs_proc_list_entry *next;
};

struct sufs_libfs_proc
{
        unsigned int pid;

        unsigned int uid;
        unsigned int gid;

        /* Current directory */
        struct sufs_libfs_mnode *cwd_m;
        struct sufs_libfs_filetable *ftable;
};

extern struct sufs_libfs_proc_list_entry *sufs_libfs_proc_list_head;

static inline struct sufs_libfs_proc* sufs_libfs_current_proc(void)
{
    return (sufs_libfs_proc_list_head->item);
}

static inline void sufs_libfs_proc_list_delete(
        struct sufs_libfs_proc_list_entry *prev,
        struct sufs_libfs_proc_list_entry *curr)
{
    if (!prev)
    {
        sufs_libfs_proc_list_head = curr->next;
    }
    else
    {
        prev->next = curr->next;
    }

    free(curr);
}

struct sufs_libfs_proc* sufs_libfs_proc_init(void);

int sufs_libfs_proc_list_insert(struct sufs_libfs_proc *proc);

void sufs_libfs_proc_destroy(struct sufs_libfs_proc *proc);

#endif /* PROC_H_ */
