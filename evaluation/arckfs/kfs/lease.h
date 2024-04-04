#ifndef SUFS_KFS_LEASE_H_
#define SUFS_KFS_LEASE_H_

#include <linux/spinlock.h>
#include <linux/sched.h>

#include "../include/kfs_config.h"

/*
 * Just need something simple here to make the prototype work, the release is
 * not the interesting part of the paper and it is unlikely to become the
 * performance bottleneck.
 */

struct sufs_kfs_lease
{
    spinlock_t lock;

    int state;

    int owner_cnt;
    pid_t owner[SUFS_KFS_LEASE_MAX_OWNER];
    unsigned long lease_tsc[SUFS_KFS_LEASE_MAX_OWNER];
};

int sufs_kfs_acquire_write_lease(int ino, struct sufs_kfs_lease * l, int tgid);

int sufs_kfs_acquire_read_lease(int ino, struct sufs_kfs_lease * l, int tgid);

int sufs_kfs_release_lease(int ino, struct sufs_kfs_lease *l, int tgid);

int sufs_kfs_renew_lease(int ino);

static inline void
sufs_kfs_init_lease(struct sufs_kfs_lease * l)
{
    memset(l, 0, sizeof(struct sufs_kfs_lease));
#if 0
    printk("lock addr is %lx\n", (unsigned long) (&(l->lock)));
#endif

    spin_lock_init(&(l->lock));
}

#endif /* SUFS_KFS_LEASE_H_ */
