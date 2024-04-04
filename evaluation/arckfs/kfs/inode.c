#include <linux/kernel.h>
#include <linux/spinlock.h>

#include "../include/kfs_config.h"
#include "../include/ioctl.h"
#include "../include/common_inode.h"
#include "inode.h"

static inline int sufs_insert_inodetree(struct rb_root *tree,
        struct sufs_range_node *new_node)
{
    int ret = 0;

    ret = sufs_rbtree_insert_range_node(tree, new_node, NODE_INODE);
    if (ret)
        printk("ERROR: %s failed %d\n", __func__, ret);

    return ret;
}


/* init */
int sufs_alloc_inode_free_list(struct sufs_super_block * sb)
{
    struct sufs_inode_free_list * ilist = NULL;
    int i = 0;

    sb->inode_free_lists = kcalloc(sb->sockets * sb->cpus_per_socket,
            sizeof(struct sufs_inode_free_list), GFP_KERNEL);

    if (!sb->inode_free_lists) {
        printk("%s: Allocating inode maps failed.", __func__);
        return -ENOMEM;
    }

    for (i = 0; i < (sb->sockets * sb->cpus_per_socket); i++)
    {
        ilist = &(sb->inode_free_lists[i]);

        ilist->inode_free_tree = RB_ROOT;
        spin_lock_init(&ilist->lock);
    }

    return 0;
}

/* Free */
void sufs_free_inode_free_list(struct sufs_super_block * sb)
{
    if (sb->inode_free_lists)
        kfree(sb->inode_free_lists);
    sb->inode_free_lists = NULL;
}

static void __sufs_init_inode_free_list(struct sufs_super_block * sb,
        struct sufs_inode_free_list *free_list, int cpu)
{
    int per_list_inodes = 0;

    per_list_inodes = SUFS_MAX_INODE_NUM / (sb->sockets * sb->cpus_per_socket);

    if (cpu == 0)
    {
        free_list->inode_start = SUFS_FIRST_UFS_INODE_NUM;

        free_list->inode_end = per_list_inodes - 1;
    }
    else if (cpu == sb->sockets * sb->cpus_per_socket - 1)
    {
        free_list->inode_start = per_list_inodes * cpu;

        free_list->inode_end = SUFS_MAX_INODE_NUM - 1;
    }
    else
    {
        free_list->inode_start = per_list_inodes * cpu;

        free_list->inode_end = free_list->inode_start + per_list_inodes - 1;
    }
}

int sufs_init_inode_free_list(struct sufs_super_block *sb)
{
    struct sufs_range_node *range_node = NULL;
    struct sufs_inode_free_list *ilist = NULL;
    int i = 0;
    int ret = 0;

    for (i = 0; i < (sb->sockets * sb->cpus_per_socket); i++)
    {
        ilist = &(sb->inode_free_lists[i]);
        __sufs_init_inode_free_list(sb, ilist, i);

        range_node = sufs_alloc_inode_node();
        if (range_node == NULL)
            BUG();

        range_node->range_low = ilist->inode_start;
        range_node->range_high = ilist->inode_end;

        ilist->num_free_inodes = ilist->inode_end - ilist->inode_start + 1;

        ret = sufs_insert_inodetree(&(ilist->inode_free_tree), range_node);
        if (ret)
        {
            printk("%s failed\n", __func__);
            sufs_free_inode_node(range_node);
            return ret;
        }
        ilist->first_node = range_node;
        ilist->last_node = range_node;
        ilist->num_inode_node = 1;
    }

    return 0;
}

/* Which cpu this inode belongs to */
static void sufs_inode_to_cpu(struct sufs_super_block * sb, int ino,
                     int *cpu)
{
    int per_list_inodes = 0;
    int cpu_tmp = 0;

    per_list_inodes = SUFS_MAX_INODE_NUM / (sb->sockets * sb->cpus_per_socket);

    cpu_tmp = ino / per_list_inodes;

    if (cpu)
        (*cpu) = cpu_tmp;

    return;
}



static int sufs_inode_find_free_slot(struct rb_root *tree,
            int range_low, int range_high,
            struct sufs_range_node **prev, struct sufs_range_node **next)
{
    struct sufs_range_node *ret_node = NULL;
    struct rb_node *tmp = NULL;
    int ret = 0;

    ret = sufs_rbtree_find_range_node(tree, range_low, NODE_INODE, &ret_node);
    if (ret)
    {
        printk("%s ERROR: %d - %d already in free list\n", __func__,
             range_low, range_high);
        return -EINVAL;
    }

    if (!ret_node)
    {
        *prev = *next = NULL;
    }
    else if (ret_node->range_high < range_low)
    {
        *prev = ret_node;
        tmp = rb_next(&ret_node->node);
        if (tmp)
        {
            *next = container_of(tmp, struct sufs_range_node, node);
        }
        else
        {
            *next = NULL;
        }
    }
    else if (ret_node->range_low > range_high)
    {
        *next = ret_node;
        tmp = rb_prev(&ret_node->node);
        if (tmp)
        {
            *prev = container_of(tmp, struct sufs_range_node, node);
        }
        else
        {
            *prev = NULL;
        }
    }
    else
    {
        printk("%s ERROR: %d - %d overlaps with existing "
             "node %ld - %ld\n",
             __func__, range_low, range_high, ret_node->range_low,
             ret_node->range_high);
        return -EINVAL;
    }

    return 0;
}

/*
 * This has the implicit assumption that the freed inode chunk only belongs to
 * one CPU pool
 */
int sufs_free_inodes(struct sufs_super_block * sb, int ino, int num)
{
    struct rb_root *tree = NULL;
    int low = 0;
    int high = 0;
    struct sufs_range_node *prev = NULL;
    struct sufs_range_node *next = NULL;
    struct sufs_range_node *curr_node = NULL;
    struct sufs_inode_free_list *free_list = NULL;
    int cpu = 0;
    int new_node_used = 0;
    int ret = 0;

    if (num <= 0)
    {
        printk("%s ERROR: free inode: %d\n", __func__, num);
        return -EINVAL;
    }

    sufs_inode_to_cpu(sb, ino, &cpu);

    /* Pre-allocate node */
    curr_node = sufs_alloc_inode_node();
    if (curr_node == NULL)
    {
        /* returning without freeing the node*/
        return -ENOMEM;
    }

    free_list = &(sb->inode_free_lists[cpu]);

    spin_lock(&free_list->lock);

    tree = &(free_list->inode_free_tree);

    low = ino;
    high = ino + num - 1;

    if (ino < free_list->inode_start ||
            ino + num > free_list->inode_end + 1)
    {
        printk("free inodes %d to %d, free list addr: %lx, "
             "start %d, end %d\n",
             ino, ino + num - 1,
             (unsigned long)free_list, free_list->inode_start,
             free_list->inode_end);

        ret = -EIO;
        goto out;
    }

    ret = sufs_inode_find_free_slot(tree, low, high, &prev, &next);

    if (ret)
    {
        printk("%s: find free slot fail: %d\n", __func__, ret);
        goto out;
    }

    if (prev && next && (low == prev->range_high + 1) &&
        (high + 1 == next->range_low))
    {
        /* fits the hole */
        rb_erase(&next->node, tree);
        free_list->num_inode_node--;
        prev->range_high = next->range_high;
        if (free_list->last_node == next)
            free_list->last_node = prev;

        sufs_free_inode_node(next);
        goto found;
    }

    if (prev && (low == prev->range_high + 1))
    {
        /* Aligns left */
        prev->range_high += num;
        goto found;
    }

    if (next && (high + 1 == next->range_low))
    {
        /* Aligns right */
        next->range_low -= num;
        goto found;
    }

    /* Aligns somewhere in the middle */
    curr_node->range_low = low;
    curr_node->range_high = high;

    new_node_used = 1;
    ret = sufs_insert_inodetree(tree, curr_node);
    if (ret)
    {
        new_node_used = 0;
        goto out;
    }

    if (!prev)
        free_list->first_node = curr_node;
    if (!next)
        free_list->last_node = curr_node;

    free_list->num_inode_node++;

found:
    free_list->num_free_inodes += num;

out:
    spin_unlock(&free_list->lock);
    if (new_node_used == 0)
        sufs_free_inode_node(curr_node);

    return ret;
}


static int not_enough_inodes(struct sufs_inode_free_list *free_list,
                 int num_inodes)
{
    struct sufs_range_node *first = free_list->first_node;
    struct sufs_range_node *last = free_list->last_node;

   /*
    * free_list->num_free_inodes / free_list->num_inode_node is used to handle
    * fragmentation within nodes
    */
    if (!first || !last ||
        free_list->num_free_inodes / free_list->num_inode_node <
        num_inodes)
    {
        printk("%s: num_free_inodes=%d; num_inodes=%d; "
                   "first=0x%p; last=0x%p",
                   __func__, free_list->num_free_inodes, num_inodes,
                   first, last);
        return 1;
    }

    return 0;
}

/* Find out the free list with most free inodes */
static int sufs_get_candidate_free_list(struct sufs_super_block * sb)
{
    struct sufs_inode_free_list *free_list = NULL;
    int cpuid = 0;
    int num_free_inodes = 0;
    int i = 0;

    for (i = 0; i < (sb->cpus_per_socket) * (sb->sockets); i++)
    {
        free_list = &(sb->inode_free_lists[i]);
        if (free_list->num_free_inodes > num_free_inodes)
        {
            cpuid = i;
            num_free_inodes = free_list->num_free_inodes;
        }
    }

    return cpuid;
}

/* Return how many inodes allocated */
static int sufs_alloc_inodes_in_free_list(struct sufs_super_block * sb,
                       struct sufs_inode_free_list *free_list,
                       int num_inodes,
                       int *new_inode_nr)
{
    struct rb_root *tree = NULL;
    struct sufs_range_node *curr = NULL, *next = NULL, *prev = NULL;
    struct rb_node *temp = NULL, *next_node = NULL, *prev_node = NULL;
    int curr_inodes = 0;
    bool found = 0;

    int step = 0;

    if (!free_list->first_node || free_list->num_free_inodes == 0)
    {
        printk("%s: Can't alloc. inode_free_list->first_node=0x%p "
             "inode_free_list->num_free_inodeks = %d",
             __func__, free_list->first_node,
             free_list->num_free_inodes);
        return -ENOSPC;
    }

    tree = &(free_list->inode_free_tree);
    temp = &(free_list->first_node->node);

    /* always use the unaligned approach */
    while (temp)
    {
        step++;
        curr = container_of(temp, struct sufs_range_node, node);

        curr_inodes = curr->range_high - curr->range_low + 1;

        if (num_inodes >= curr_inodes)
        {
            if (num_inodes > curr_inodes)
                goto next;

            /* Otherwise, allocate the whole node */
            if (curr == free_list->first_node)
            {
                next_node = rb_next(temp);
                if (next_node)
                {
                    next = container_of(
                        next_node,
                        struct sufs_range_node, node);
                }

                free_list->first_node = next;
            }

            if (curr == free_list->last_node)
            {
                prev_node = rb_prev(temp);
                if (prev_node)
                {
                    prev = container_of(
                        prev_node,
                        struct sufs_range_node, node);
                }

                free_list->last_node = prev;
            }

            rb_erase(&curr->node, tree);
            free_list->num_inode_node--;
            num_inodes = curr_inodes;
            *new_inode_nr = curr->range_low;
            sufs_free_inode_node(curr);
            found = 1;
            break;
        }

        /* Allocate partial inode_node */

        *new_inode_nr = curr->range_low;
        curr->range_low += num_inodes;

        found = 1;
        break;
    next:
        temp = rb_next(temp);
    }

    if (free_list->num_free_inodes < num_inodes)
    {
        printk("%s: free list has %d free inodes, "
             "but allocated %d inodes?\n",
             __func__, free_list->num_free_inodes, num_inodes);
        return -ENOSPC;
    }

    if (found == 1)
    {
        free_list->num_free_inodes -= num_inodes;
    }
    else
    {
        printk("%s: Can't alloc.  found = %d", __func__, found);
        return -ENOSPC;
    }

    return num_inodes;
}

int sufs_new_inodes(struct sufs_super_block * sb, int *inode_nr,
               int num_inodes, int zero, int cpu)
{
    struct sufs_inode_free_list *free_list = NULL;
    int new_inode_nr = 0;
    int ret_inodes = 0;
    int retried = 0;

    if (num_inodes == 0)
    {
        printk("%s: num_inodes == 0", __func__);
        return -EINVAL;
    }

retry:
    free_list = &(sb->inode_free_lists[cpu]);
    spin_lock(&free_list->lock);

    if (not_enough_inodes(free_list, num_inodes))
    {
        printk(
            "%s: cpu %d, free_inodes %d, required %d, "
            "inode_node %d\n",
            __func__, cpu, free_list->num_free_inodes,
            num_inodes, free_list->num_inode_node);

        if (retried >= 2)
            /* Allocate anyway */
            goto alloc;

        spin_unlock(&free_list->lock);
        cpu = sufs_get_candidate_free_list(sb);
        retried++;
        goto retry;
    }

alloc:
    ret_inodes = sufs_alloc_inodes_in_free_list(sb, free_list, num_inodes,
                                &new_inode_nr);

    spin_unlock(&free_list->lock);

    if (ret_inodes <= 0 || new_inode_nr == 0)
    {
        printk("%s: not able to allocate %d inodes. "
             "ret_inodes=%d; new_indoe_nr=%d",
             __func__, num_inodes, ret_inodes, new_inode_nr);
        return -ENOSPC;
    }

    if (zero)
    {
        /* TODO: Do we need this for inode? */
    }

    if (inode_nr)
        *inode_nr = new_inode_nr;

    return ret_inodes;
}

int sufs_alloc_inode_to_libfs(unsigned long uaddr)
{
    struct sufs_ioctl_inode_alloc_entry entry;

    int inode_nr = 0, ret = 0, cpu = 0;

    /*
     * TODO: Should perform more checks here to validate the results
     * obtained from the user
     */

    if (copy_from_user(&entry, (void *) uaddr,
            sizeof(struct sufs_ioctl_inode_alloc_entry)))
        return -EFAULT;

    cpu = entry.cpu == -1 ? smp_processor_id() : entry.cpu;

    ret = sufs_new_inodes(&sufs_sb, &inode_nr,
            entry.num, 0, cpu);

    if (ret < 0)
        return ret;

    entry.num = ret;
    entry.inode = inode_nr;

    if (copy_to_user((void *) uaddr, &entry,
            sizeof(struct sufs_ioctl_inode_alloc_entry)))
        return -EFAULT;

    return 0;
}

int sufs_free_inode_from_libfs(unsigned long uaddr)
{
    struct sufs_ioctl_inode_alloc_entry entry;

     /*
      * TODO: Should perform more checks here to validate the results
      * obtained from the user
      */

     if (copy_from_user(&entry, (void *) uaddr,
             sizeof(struct sufs_ioctl_inode_alloc_entry)))
         return -EFAULT;


     return sufs_free_inodes(&sufs_sb, entry.inode, entry.num);
}


/* create an new inode */
int sufs_kfs_set_inode(int ino, char type, unsigned int mode,
        unsigned int uid, unsigned int gid, unsigned long index_offset)
{
    struct sufs_shadow_inode * si = sufs_find_sinode(ino);
    int already_exist = (si->file_type != SUFS_FILE_TYPE_NONE);

    if (si == NULL)
    {
        printk("Cannot find the shadow indode for inode : %d\n", ino);
        return -EFAULT;
    }

    si->file_type = type;
    si->mode = mode;
    si->uid = uid;
    si->gid = gid;
    si->index_offset = index_offset;

#if 0
    printk("ino is %d, si is %lx, lease is %lx\n", ino,
            (unsigned long )si, (unsigned long) (&si->lease));
#endif


    if (!already_exist)
        sufs_kfs_init_lease(&si->lease);

    return 0;
}


