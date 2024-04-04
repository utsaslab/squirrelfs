#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/sched.h>

#include "../include/kfs_config.h"
#include "util.h"
#include "tgroup.h"
#include "lease.h"
#include "super.h"
#include "inode.h"

/*
 * Just need something simple here to make the prototype work, the release is
 * not the interesting part of the paper and it is unlikely to become the
 * performance bottleneck.
 */


static struct sufs_kfs_lease* sufs_kfs_get_lease(int ino)
{
    struct sufs_shadow_inode *sinode = NULL;

    sinode = sufs_find_sinode(ino);

    if (sinode == NULL)
    {
        printk("Cannot find sinode with ino %d\n", ino);
        return NULL;
    }

    return &(sinode->lease);
}

/* return true if we need to check the expire condition of lease */
static inline int sufs_kfs_lease_need_check_expire(struct sufs_kfs_lease *l,
        int new_state)
{
    return (new_state == SUFS_KFS_WRITE_OWNED
            || l->state == SUFS_KFS_WRITE_OWNED);
}

static inline int sufs_kfs_is_lease_expired(int ino, struct sufs_kfs_lease *l,
        int index)
{
    unsigned long *lease_ring_addr = NULL;
    if (l->lease_tsc[index] + SUFS_KFS_LEASE_PERIOD < sufs_kfs_rdtsc())
        return 1;

    lease_ring_addr = sufs_tgroup[l->owner[index]].lease_ring_kaddr;

    return !(test_bit(ino, lease_ring_addr));

}

/* Invoke when the lock is acquired
 *
 * return 1 if the lease can be acquired,
 * otherwise SUFS_KFS_ERROR*
 *
 * For write, one can acquire the lease if
 * 1. The lease is unowned
 * 2. Its trust group has not acquired the lease
 * 3. all the previous leases expired.
 *
 * For read, one can acquire the lease if
 * 1. The lease is unowned
 * 2. Its trust group has not acquired the lease
 * 3. The previous lease is WRITE_OWNED and it has expired
 * 4. The previous lease is READ_OWNED and it still has space to add one more
 *    trust group
 */

static int sufs_kfs_can_acquire(int ino, struct sufs_kfs_lease *l, int tgid,
        int new_state)
{
    int i = 0, ret = 1;

    if (l->state == SUFS_KFS_UNOWNED)
        return ret;

    /*
     * Upgrade a read lease with a write lease, disable it for now since
     * I have not implemented the way to change the permission of the mapping
     */

    /*
     if (new_state == SUFS_KFS_WRITE_OWNED && l->state == SUFS_KFS_READ_OWNED
            && l->owner_cnt == 1 && l->owner[0] == tgid)
        return ret;
    */

    for (i = 0; i < l->owner_cnt; i++)
    {
        /* Try to acquire a lease that is already granted */
        if (l->owner[i] == tgid)
            return -EINVAL;

        if (sufs_kfs_lease_need_check_expire(l, new_state))
        {
            /* Check whether there is unexpired lease */
            if (!sufs_kfs_is_lease_expired(ino, l, i))
                return -EAGAIN;
        }
    }

    if (new_state == SUFS_KFS_READ_OWNED && l->state == SUFS_KFS_READ_OWNED)
    {
        if (l->owner_cnt == SUFS_KFS_LEASE_MAX_OWNER)
            return -ENOSPC;
    }

    return ret;
}

/*
 * Garbage collect the unused entry in the lease.
 * Not intended for performance, just make it as simple as possible
 *
 * Invoked when the lock is held
 */
static void sufs_kfs_gc_lease(struct sufs_kfs_lease *l)
{
    int i = 0;

    /* XXX: The below two arrays might overflow with a
     large SUFS_KFS_LEASE_MAX_OWNER */
    pid_t sowner[SUFS_KFS_LEASE_MAX_OWNER];
    unsigned long slease_tsc[SUFS_KFS_LEASE_MAX_OWNER];
    int sowner_cnt = 0;

    for (i = 0; i < l->owner_cnt; i++)
    {
        if (l->owner[i] != 0)
        {
            sowner[sowner_cnt] = l->owner[i];
            slease_tsc[sowner_cnt] = l->lease_tsc[i];
            sowner_cnt++;
        }
    }

    for (i = 0; i < sowner_cnt; i++)
    {
        l->owner[i] = sowner[i];
        l->lease_tsc[i] = slease_tsc[i];
    }

    l->owner_cnt = sowner_cnt;
}

/*
 * Need metadata check when one file is transferred from one trust group to
 * another
 */
static inline int sufs_kfs_need_mcheck(struct sufs_kfs_lease *l)
{
    /*
     * We ensure that a metadata integrity check has been performed when the
     * file state is transferred to UNOWNED
     *
     * So no metadata check for this case
     *
     * For read owned, there is no chance for the ufs to modify file system
     * state.
     *
     * So only metadata check if it is transferred from write owned.
     */
    if (l->state == SUFS_KFS_UNOWNED || l->state == SUFS_KFS_READ_OWNED)
    {
        return 0;
    }

    return 1;
}

static void sufs_kfs_clean_map_ring(int ino, struct sufs_kfs_lease * l)
{
    int i = 0;

    for (i = 0; i < l->owner_cnt; i++)
    {
        int owner = l->owner[i];
        struct sufs_tgroup * tgroup = sufs_kfs_tgid_to_tgroup(owner);

        if (tgroup)
            clear_bit(ino, tgroup->map_ring_kaddr);
    }
}

int sufs_kfs_acquire_write_lease(int ino, struct sufs_kfs_lease *l, int tgid)
{
    int ret;
    unsigned long flags;

    spin_lock_irqsave(&(l->lock), flags);

    if ((ret = sufs_kfs_can_acquire(ino, l, tgid, SUFS_KFS_WRITE_OWNED)) > 0)
    {

        if (sufs_kfs_need_mcheck(l))
        {
            /* TODO: perform metadata check */
        }

        sufs_kfs_clean_map_ring(ino, l);

        l->state = SUFS_KFS_WRITE_OWNED;
        l->owner_cnt = 1;
        l->owner[0] = tgid;
        l->lease_tsc[0] = sufs_kfs_rdtsc();

        ret = 0;
    }

    spin_unlock_irqrestore(&(l->lock), flags);

    return ret;
}

/*
 * Test whether a trust group has acquired the lock or not
 * Invoke when holding the lock
 */
static int sufs_kfs_is_acquired_lock(struct sufs_kfs_lease *l, int tgid,
        int *index)
{
    if (l->state == SUFS_KFS_UNOWNED)
        return 0;
    else if (l->state == SUFS_KFS_WRITE_OWNED)
    {
        return (tgid == l->owner[0]);
    }
    /* read owned case */
    else
    {
        int i = 0;
        for (i = 0; i < l->owner_cnt; i++)
        {
            if (tgid == l->owner[i])
            {
                if (index)
                    (*index) = i;
                return 1;
            }
        }

        return 0;
    }
}

int sufs_kfs_release_lease(int ino, struct sufs_kfs_lease *l, int tgid)
{
    unsigned long flags;
    int ret = 0, index = 0;

    spin_lock_irqsave(&(l->lock), flags);

    /* check whether the lease has been acquired by the current trust group */
    if (!sufs_kfs_is_acquired_lock(l, tgid, &index))
    {
        ret = -EINVAL;
        goto out;
    }

    if (l->state == SUFS_KFS_WRITE_OWNED)
    {

        /* TODO: Perform metadata check */
        l->state = SUFS_KFS_UNOWNED;
        l->owner_cnt = 0;
        ret = 0;
    }
    else
    {
        l->owner[index] = 0;
        l->lease_tsc[index] = 0;
        sufs_kfs_gc_lease(l);

        if (l->owner_cnt == 0)
        {
            /* TODO: Perform metadata check */
            l->state = SUFS_KFS_UNOWNED;
        }

        ret = 0;
    }

out:
    spin_unlock_irqrestore(&(l->lock), flags);
    return ret;
}




int sufs_kfs_acquire_read_lease(int ino, struct sufs_kfs_lease *l, int tgid)
{
    int ret = 0;
    unsigned long flags = 0;

#if 0
    printk("lock addr is %lx\n", (unsigned long) (&(l->lock)));
#endif

    spin_lock_irqsave(&(l->lock), flags);

    if ((ret = sufs_kfs_can_acquire(ino, l, tgid, SUFS_KFS_READ_OWNED)) > 0)
    {

        if (sufs_kfs_need_mcheck(l))
        {
            /* TODO: perform metadata check */
        }

        if (l->state == SUFS_KFS_READ_OWNED)
        {
            l->owner[l->owner_cnt] = tgid;
            l->lease_tsc[l->owner_cnt] = sufs_kfs_rdtsc();
            l->owner_cnt++;
        }
        else
        {
            l->state = SUFS_KFS_READ_OWNED;
            l->owner_cnt = 1;
            l->owner[0] = tgid;
            l->lease_tsc[0] = sufs_kfs_rdtsc();
        }

        ret = 0;
    }

    spin_unlock_irqrestore(&(l->lock), flags);

    return ret;
}


int sufs_kfs_renew_lease(int ino)
{
    struct sufs_kfs_lease *l = sufs_kfs_get_lease(ino);
    unsigned long flags;
    int ret = 0, index = 0;

    int tgid = sufs_kfs_pid_to_tgid(current->tgid, 0);

    spin_lock_irqsave(&(l->lock), flags);

    /* check whether the lease has been acquired by the current trust group */
    if (!sufs_kfs_is_acquired_lock(l, tgid, &index))
    {
        ret = -EINVAL;
        goto out;
    }

    l->lease_tsc[index] = sufs_kfs_rdtsc();
out:
    spin_unlock_irqrestore(&(l->lock), flags);
    return ret;
}
