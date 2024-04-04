/*
 * Mostly Copied from NOVA persistent memory management
 *
 * Copyright 2015-2016 Regents of the University of California,
 * UCSD Non-Volatile Systems Lab, Andiry Xu <jix024@cs.ucsd.edu>
 * Copyright 2012-2013 Intel Corporation
 * Copyright 2009-2011 Marco Stornelli <marco.stornelli@gmail.com>
 * Copyright 2003 Sony Corporation
 * Copyright 2003 Matsushita Electric Industrial Co., Ltd.
 * 2003-2004 (c) MontaVista Software, Inc. , Steve Longerbeam
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <linux/bitops.h>
#include <linux/fs.h>
#include <linux/percpu.h>
#include <linux/random.h>
#include <linux/rbtree.h>

#include "../include/kfs_config.h"
#include "../include/ioctl.h"
#include "../include/ring_buffer.h"

#include "balloc.h"
#include "rbtree.h"
#include "super.h"
#include "tgroup.h"
#include "mmap.h"
#include "delegation.h"


static int sufs_insert_blocktree(struct rb_root *tree,
        struct sufs_range_node *new_node)
{
    int ret = 0;

    ret = sufs_rbtree_insert_range_node(tree, new_node, NODE_BLOCK);
    if (ret)
        printk("ERROR: %s failed %d\n", __func__, ret);

    return ret;
}

/* init */

int sufs_alloc_block_free_lists(struct sufs_super_block * sb)
{
    struct sufs_free_list *free_list = NULL;
    int i = 0, j = 0;

    int cpus = sb->cpus_per_socket * sb->sockets;

    sb->free_lists = kcalloc(cpus * sb->pm_nodes,
                  sizeof(struct sufs_free_list), GFP_KERNEL);

    if (!sb->free_lists)
        return -ENOMEM;

    for (i = 0; i < cpus; i++)
    {
        for (j = 0; j < sb->pm_nodes; j++)
        {
            free_list = sufs_get_free_list(sb, i, j);
            free_list->block_free_tree = RB_ROOT;
            spin_lock_init(&free_list->s_lock);
        }
    }

    return 0;
}

void sufs_delete_block_free_lists(struct sufs_super_block * sb)
{
    /* Each tree is freed in save_blocknode_mappings */
    if (sb->free_lists)
        kfree(sb->free_lists);
    sb->free_lists = NULL;
}

/*
 * Initialize a free list.  Each CPU gets an equal share of the block space to
 * manage.
 */

static void sufs_init_free_list(struct sufs_super_block * sb,
                struct sufs_free_list *free_list, int cpu,
                int pm_node)
{
    unsigned long per_list_blocks = 0;

    int cpus = sb->cpus_per_socket * sb->sockets;

    unsigned long size = sb->pm_node_info[pm_node].end_block -
                 sb->pm_node_info[pm_node].start_block + 1;

    per_list_blocks = size / cpus;

    if (cpu != cpus - 1)
    {
        free_list->block_start = sb->pm_node_info[pm_node].start_block +
                     per_list_blocks * cpu;

        free_list->block_end = free_list->block_start + per_list_blocks - 1;
    }
    else
    {
        free_list->block_start = sb->pm_node_info[pm_node].start_block +
                     per_list_blocks * cpu;

        free_list->block_end = sb->pm_node_info[pm_node].end_block;
    }

    if (cpu == 0 && pm_node == sb->head_node)
    {
        free_list->block_start += sb->head_reserved_blocks;

        if (free_list->block_start >= free_list->block_end)
        {
            printk("Node overflow!\n");
            free_list->block_start = free_list->block_end;
        }
    }
}

void sufs_init_block_free_list(struct sufs_super_block * sb, int recovery)
{
    struct rb_root *tree = NULL;
    struct sufs_range_node *blknode = NULL;
    struct sufs_free_list *free_list = NULL;
    int i = 0, j = 0, cpus = 0;
    int ret = 0;

    cpus = sb->cpus_per_socket * sb->sockets;

    /* Divide the block range among per-CPU free lists */
    for (i = 0; i < cpus; i++)
        for (j = 0; j < sb->pm_nodes; j++)
        {
            free_list = sufs_get_free_list(sb, i, j);
            tree = &(free_list->block_free_tree);
            sufs_init_free_list(sb, free_list, i, j);

            /* For recovery, update these fields later */
            if (recovery == 0)
            {
                free_list->num_free_blocks =
                    free_list->block_end -
                    free_list->block_start + 1;

                blknode = sufs_alloc_blocknode();

                if (blknode == NULL)
                    BUG();

                blknode->range_low = free_list->block_start;
                blknode->range_high = free_list->block_end;

                ret = sufs_insert_blocktree(tree, blknode);
                if (ret)
                {
                    printk("%s failed\n", __func__);
                    sufs_free_blocknode(blknode);
                    return;
                }
                free_list->first_node = blknode;
                free_list->last_node = blknode;
                free_list->num_blocknode = 1;
            }

#if 0
            printk(
                "%s: free list, addr: %lx, cpu %d, pm_node %d,  block "
                "start %lu, end %lu, "
                "%lu free blocks\n",
                __func__, (unsigned long)free_list, i, j,
                free_list->block_start, free_list->block_end,
                free_list->num_free_blocks);
#endif
        }
}

/* Which cpu or pm_node this block belongs to */
static void sufs_block_to_cpu_pm_nodes(struct sufs_super_block * sb, int blocknr,
                     int *cpu, int *pm_nodes)
{
    int cpu_tmp = 0;
    unsigned long size = 0;
    int tmp_pm_nodes = 0;

    int cpus = sb->cpus_per_socket * sb->sockets;

    tmp_pm_nodes = sufs_block_to_pm_node(sb, blocknr);

    if (pm_nodes)
        *pm_nodes = tmp_pm_nodes;

    size = sb->pm_node_info[tmp_pm_nodes].end_block -
           sb->pm_node_info[tmp_pm_nodes].start_block + 1;

    cpu_tmp = (blocknr - sb->pm_node_info[tmp_pm_nodes].start_block) /
          (size / cpus);

    /* The remainder of the last cpu */
    if (cpu_tmp >= cpus)
        cpu_tmp = cpus - 1;

    if (cpu)
        (*cpu) = cpu_tmp;

    return;
}

/* Used for both block free tree and inode inuse tree */
static int sufs_find_free_slot(struct rb_root *tree, unsigned long range_low,
            unsigned long range_high, struct sufs_range_node **prev,
            struct sufs_range_node **next)
{
    struct sufs_range_node *ret_node = NULL;
    struct rb_node *tmp = NULL;
    int ret = 0;

    ret = sufs_rbtree_find_range_node(tree, range_low, NODE_BLOCK, &ret_node);
    if (ret)
    {
        printk("%s ERROR: %lu - %lu already in free list\n", __func__,
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
        printk("%s ERROR: %lu - %lu overlaps with existing "
             "node %lu - %lu\n",
             __func__, range_low, range_high, ret_node->range_low,
             ret_node->range_high);
        return -EINVAL;
    }

    return 0;
}

/*
 * This has the implicit assumption that the freed block chunk only belongs to
 * one CPU pool
 */
int sufs_free_blocks(struct sufs_super_block * sb, unsigned long blocknr,
                unsigned long num_blocks)
{
    struct rb_root *tree = NULL;
    unsigned long block_low = 0;
    unsigned long block_high = 0;
    struct sufs_range_node *prev = NULL;
    struct sufs_range_node *next = NULL;
    struct sufs_range_node *curr_node = NULL;
    struct sufs_free_list *free_list = NULL;
    int cpuid = 0, pm_node = 0;
    int new_node_used = 0;
    int ret = 0;

    if (num_blocks <= 0)
    {
        printk("%s ERROR: free %lu\n", __func__, num_blocks);
        return -EINVAL;
    }

    sufs_block_to_cpu_pm_nodes(sb, blocknr, &cpuid, &pm_node);

    /* Pre-allocate blocknode */
    curr_node = sufs_alloc_blocknode();
    if (curr_node == NULL)
    {
        /* returning without freeing the block*/
        return -ENOMEM;
    }

    free_list = sufs_get_free_list(sb, cpuid, pm_node);

    spin_lock(&free_list->s_lock);

    tree = &(free_list->block_free_tree);

    block_low = blocknr;
    block_high = blocknr + num_blocks - 1;

    if (blocknr < free_list->block_start ||
        blocknr + num_blocks > free_list->block_end + 1)
    {
        printk("free blocks %lx to %lx, free list addr: %lx, "
             "start %lu, end %lu\n",
             blocknr, blocknr + num_blocks - 1,
             (unsigned long)free_list, free_list->block_start,
             free_list->block_end);

        ret = -EIO;
        goto out;
    }

    ret = sufs_find_free_slot(tree, block_low, block_high, &prev, &next);

    if (ret)
    {
        printk("%s: find free slot fail: %d\n", __func__, ret);
        goto out;
    }

    if (prev && next && (block_low == prev->range_high + 1) &&
        (block_high + 1 == next->range_low))
    {
        /* fits the hole */
        rb_erase(&next->node, tree);
        free_list->num_blocknode--;
        prev->range_high = next->range_high;
        if (free_list->last_node == next)
            free_list->last_node = prev;
        sufs_free_blocknode(next);
        goto block_found;
    }

    if (prev && (block_low == prev->range_high + 1))
    {
        /* Aligns left */
        prev->range_high += num_blocks;
        goto block_found;
    }

    if (next && (block_high + 1 == next->range_low))
    {
        /* Aligns right */
        next->range_low -= num_blocks;
        goto block_found;
    }

    /* Aligns somewhere in the middle */
    curr_node->range_low = block_low;
    curr_node->range_high = block_high;

    new_node_used = 1;
    ret = sufs_insert_blocktree(tree, curr_node);
    if (ret)
    {
        new_node_used = 0;
        goto out;
    }

    if (!prev)
        free_list->first_node = curr_node;
    if (!next)
        free_list->last_node = curr_node;

    free_list->num_blocknode++;

block_found:
    free_list->num_free_blocks += num_blocks;

out:
    spin_unlock(&free_list->s_lock);
    if (new_node_used == 0)
        sufs_free_blocknode(curr_node);

    return ret;
}

static int not_enough_blocks(struct sufs_free_list *free_list,
                 unsigned long num_blocks)
{
    struct sufs_range_node *first = free_list->first_node;
    struct sufs_range_node *last = free_list->last_node;

   /*
    * free_list->num_free_blocks / free_list->num_blocknode is used to handle
    * fragmentation within blocknodes
    */
    if (!first || !last ||
        free_list->num_free_blocks / free_list->num_blocknode <
            num_blocks)
    {
        // printk("%s: num_free_blocks=%ld; num_blocks=%ld; "
        //            "first=0x%p; last=0x%p\n",
        //            __func__, free_list->num_free_blocks, num_blocks,
        //            first, last);
        return 1;
    }

    return 0;
}

/* Find out the free list with most free blocks */
static int sufs_get_candidate_free_list(struct sufs_super_block * sb,
        int pm_node)
{
    struct sufs_free_list *free_list = NULL;
    int cpuid = 0;
    int num_free_blocks = 0;
    int i = 0, cpus = 0;

    cpus = sb->cpus_per_socket * sb->sockets;

    for (i = 0; i < cpus; i++)
    {
        free_list = sufs_get_free_list(sb, i, pm_node);
        if (free_list->num_free_blocks > num_free_blocks)
        {
            cpuid = i;
            num_free_blocks = free_list->num_free_blocks;
        }
    }

    return cpuid;
}

/* Return how many blocks allocated */
static long sufs_alloc_blocks_in_free_list(struct sufs_super_block * sb,
                       struct sufs_free_list *free_list,
                       unsigned long num_blocks,
                       unsigned long *new_blocknr)
{
    struct rb_root *tree = NULL;
    struct sufs_range_node *curr = NULL, *next = NULL, *prev = NULL;
    struct rb_node *temp = NULL, *next_node = NULL, *prev_node = NULL;
    unsigned long curr_blocks = 0;
    bool found = 0;

    unsigned long step = 0;

    if (!free_list->first_node || free_list->num_free_blocks == 0)
    {
        printk("%s: Can't alloc. free_list->first_node=0x%p "
             "free_list->num_free_blocks = %lu\n",
             __func__, free_list->first_node,
             free_list->num_free_blocks);
        return -ENOSPC;
    }

    tree = &(free_list->block_free_tree);
    temp = &(free_list->first_node->node);

    /* always use the unaligned approach */
    while (temp)
    {
        step++;
        curr = container_of(temp, struct sufs_range_node, node);

        curr_blocks = curr->range_high - curr->range_low + 1;

        if (num_blocks >= curr_blocks)
        {
            if (num_blocks > curr_blocks)
                goto next;

            /* Otherwise, allocate the whole blocknode */
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
            free_list->num_blocknode--;
            num_blocks = curr_blocks;
            *new_blocknr = curr->range_low;
            sufs_free_blocknode(curr);
            found = 1;
            break;
        }

        /* Allocate partial blocknode */

        *new_blocknr = curr->range_low;
        curr->range_low += num_blocks;

        found = 1;
        break;
    next:
        temp = rb_next(temp);
    }

    if (free_list->num_free_blocks < num_blocks)
    {
        printk("%s: free list has %lu free blocks, "
             "but allocated %lu blocks?\n",
             __func__, free_list->num_free_blocks, num_blocks);
        return -ENOSPC;
    }

    if (found == 1)
    {
        free_list->num_free_blocks -= num_blocks;
    }
    else
    {
        printk("%s: Can't alloc.  found = %d\n", __func__, found);
        return -ENOSPC;
    }

    return num_blocks;
}

static void sufs_kfs_zero_blocks(struct sufs_super_block * sb,
        unsigned long blocknr, int num)
{
    long issued_cnt[SUFS_PM_MAX_INS];
    struct sufs_notifyer completed_cnt[SUFS_PM_MAX_INS];

    if (sufs_kfs_delegation &&
            num * SUFS_PAGE_SIZE >= SUFS_WRITE_DELEGATION_LIMIT)
    {
        memset(issued_cnt, 0, sizeof(long) * SUFS_PM_MAX_INS);
        memset(completed_cnt, 0,
               sizeof(struct sufs_notifyer) * SUFS_PM_MAX_INS);

        sufs_kfs_do_clear_delegation(sb, sufs_kfs_block_to_offset(blocknr),
                PAGE_SIZE * num, 1, 1, issued_cnt,
                completed_cnt);

        sufs_kfs_complete_delegation(sb, issued_cnt, completed_cnt);
    }
    else
    {
        int i = 0;
        unsigned long vaddr = sufs_kfs_block_to_virt_addr(blocknr);

        for (i = 0; i < num; i++)
        {
            memset((void *) (vaddr + i * PAGE_SIZE), 0, PAGE_SIZE);
            if (need_resched())
            {
                cond_resched();
            }
        }
    }
}

int sufs_new_blocks(struct sufs_super_block * sb, unsigned long *blocknr,
               unsigned long num_blocks, int zero, int cpu,
               int pm_node)
{
    struct sufs_free_list *free_list = NULL;
    unsigned long new_blocknr = 0;
    long ret_blocks = 0;
    int retried = 0;

    if (num_blocks == 0)
    {
        printk("%s: num_blocks == 0\n", __func__);
        return -EINVAL;
    }

retry:
    free_list = sufs_get_free_list(sb, cpu, pm_node);
    spin_lock(&free_list->s_lock);

    if (not_enough_blocks(free_list, num_blocks))
    {
        // printk(
        //     "%s: cpu %d, pm_node: %d, free_blocks %lu, required %lu, "
        //     "blocknode %lu\n",
        //     __func__, cpu, pm_node, free_list->num_free_blocks,
        //     num_blocks, free_list->num_blocknode);

        if (retried >= 2)
            /* Allocate anyway */
            goto alloc;

        spin_unlock(&free_list->s_lock);
        cpu = sufs_get_candidate_free_list(sb, pm_node);
        retried++;
        goto retry;
    } 
    // else {
    //     printk("%s: enough free blocks (%lu free, %lu requested) on cpu %d\n", 
    //         __func__, free_list->num_free_blocks, num_blocks, cpu);
    // }

alloc:
    ret_blocks = sufs_alloc_blocks_in_free_list(sb, free_list, num_blocks,
                            &new_blocknr);

    // printk("%s: unlock free list %d\n", __func__, cpu);
    spin_unlock(&free_list->s_lock);
    // printk("%s: unlocked free list %d\n", __func__, cpu);

    if (ret_blocks <= 0 || new_blocknr == 0)
    {
        printk("%s: not able to allocate %lu blocks. "
             "ret_blocks=%ld; new_blocknr=%lu\n",
             __func__, num_blocks, ret_blocks, new_blocknr);
        return -ENOSPC;
    }

    if (zero)
        sufs_kfs_zero_blocks(sb, new_blocknr, ret_blocks);

    if (blocknr)
        *blocknr = new_blocknr;

    return ret_blocks;
}


unsigned long sufs_count_free_blocks(struct sufs_super_block * sb)
{
    struct sufs_free_list *free_list = NULL;
    unsigned long num_free_blocks = 0;
    int i = 0, j = 0, cpus = 0;

    cpus= sb->cpus_per_socket * sb->sockets;

    for (i = 0; i < cpus; i++)
    {
        for (j = 0; j < sb->pm_nodes; j++)
        {
            free_list = sufs_get_free_list(sb, i, j);
            num_free_blocks += free_list->num_free_blocks;
        }

    }

    return num_free_blocks;
}

unsigned long sufs_sys_info_libfs(unsigned long arg)
{
    struct sufs_ioctl_sys_info_entry entry;
    struct sufs_pm_node_info pm_node_info[SUFS_PM_MAX_INS];
    int i = 0;

    if (copy_from_user(&entry, (void *) arg,
            sizeof(struct sufs_ioctl_sys_info_entry)))
    {
        return -EFAULT;
    }

    for (i = 0; i < sufs_sb.pm_nodes; i++)
    {
        pm_node_info[i].start_block = sufs_sb.pm_node_info[i].start_block;

        pm_node_info[i].end_block = sufs_sb.pm_node_info[i].end_block;
    }

    if (copy_to_user(entry.raddr, pm_node_info,
            sizeof(struct sufs_pm_node_info) * sufs_sb.pm_nodes))
        return -EFAULT;

    entry.pmnode_num = sufs_sb.pm_nodes;
    entry.cpus_per_socket = sufs_sb.cpus_per_socket;
    entry.sockets = sufs_sb.sockets;
    entry.dele_ring_per_node = sufs_sb.dele_ring_per_node;

    if (copy_to_user((void *) arg, &entry,
            sizeof(struct sufs_ioctl_sys_info_entry)))
        return -EFAULT;


    return 0;
}

/* map the allocated pages to user level */
static int sufs_map_allocated_pages(unsigned long start_blk, unsigned long num)
{
    struct vm_area_struct * vma = NULL;

    int tgid = sufs_kfs_pid_to_tgid(current->tgid, 0);

    struct sufs_tgroup * tgroup = sufs_kfs_tgid_to_tgroup(tgid);

    pgprot_t prop;

    if (tgroup == NULL)
    {
        printk("Cannot find the tgroup with pid :%d\n", current->tgid);
        return -ENODEV;
    }

    vma = tgroup->mount_vma;

    if (vma == NULL)
    {
        printk("Cannot find the mapped vma\n");
        return -ENODEV;
    }

    prop = vm_get_page_prot(VM_READ | VM_WRITE | VM_SHARED);

    sufs_map_pages(vma,
            SUFS_MOUNT_ADDR + sufs_kfs_block_to_offset(start_blk),
            sufs_kfs_block_to_pfn(start_blk),
            prop,
            num);

    return 0;
}

int sufs_alloc_blocks_to_libfs(unsigned long uaddr)
{
    struct sufs_ioctl_block_alloc_entry entry;

    int ret = 0, cpu = 0;
    unsigned long block_nr = 0;

    /*
     * TODO: Should perform more checks here to validate the results
     * obtained from the user
     */

    if (copy_from_user(&entry, (void *) uaddr,
            sizeof(struct sufs_ioctl_block_alloc_entry)))
        return -EFAULT;

    cpu = entry.cpu == -1 ? smp_processor_id() : entry.cpu;


    ret = sufs_new_blocks(&sufs_sb, &block_nr, entry.num, 1, cpu,
            entry.pmnode);

    if (ret < 0)
        return ret;

    entry.num = ret;
    entry.block = block_nr;


    ret = sufs_map_allocated_pages(block_nr, ret);

    if (ret < 0)
        return ret;

    /* TODO: When the map failed, we should free the allocated pages */

    if (copy_to_user((void *) uaddr, &entry,
            sizeof(struct sufs_ioctl_inode_alloc_entry)))
        return -EFAULT;

    return 0;
}

/* map the allocated pages to user level */
static void sufs_unmap_allocated_pages(unsigned long start_blk, unsigned long num)
{
    struct vm_area_struct * vma = NULL;

    int tgid = sufs_kfs_pid_to_tgid(current->tgid, 0);

    struct sufs_tgroup * tgroup = sufs_kfs_tgid_to_tgroup(tgid);

    if (tgroup == NULL)
    {
        printk("Cannot find the tgroup with pid :%d\n", current->tgid);
        return;
    }

    vma = tgroup->mount_vma;

    if (vma == NULL)
    {
        printk("Cannot find the mapped vma\n");
        return;
    }

    zap_vma_ptes(vma, SUFS_MOUNT_ADDR + sufs_kfs_block_to_offset(start_blk),
            PAGE_SIZE * num);

}

int sufs_free_blocks_from_libfs(unsigned long uaddr)
{
    struct sufs_ioctl_block_alloc_entry entry;
     /*
      * TODO: Should perform more checks here to validate the results
      * obtained from the user
      */

     if (copy_from_user(&entry, (void *) uaddr,
             sizeof(struct sufs_ioctl_block_alloc_entry)))
         return -EFAULT;


     sufs_unmap_allocated_pages(entry.block, entry.num);

     return sufs_free_blocks(&sufs_sb, entry.block, entry.num);
}


/* Grave Yard */
#if 0


int sufs_free_data_blocks(struct sufs_super_block * sb, unsigned long blocknr,
              int num)
{
    int ret;

    if (blocknr == 0)
    {
        printk("%s: ERROR: %lu, %d\n", __func__, blocknr, num);
        return -EINVAL;
    }

    ret = sufs_free_blocks(sb, blocknr, num);

    return ret;
}

int sufs_free_index_blocks(struct sufs_super_block * sb, unsigned long blocknr,
               int num)
{
    int ret;

    if (blocknr == 0)
    {
        printk("%s: ERROR: %lu, %d\n", __func__, blocknr, num);
        return -EINVAL;
    }

    ret = sufs_free_blocks(sb, blocknr, num);

    return ret;
}

/*
 * These are the functions to call the allocator to get blocks. Let's also
 * make decisions based on allocation functions in these functions
 */

/*
 * Allocate data blocks; blocks to hold file data
 * The offset for the allocated block comes back in
 * blocknr.  Return the number of blocks allocated.
 */

static int sufs_do_new_data_blocks(struct super_block *sb,
                   struct sufs_inode *inode,
                   unsigned long *blocknr, unsigned int num,
                   int zero, int curr_cpu)
{
    int allocated;

    int num_blocks = num * sufs_get_numblocks(inode->i_blk_type);

    allocated = sufs_new_blocks(sb, blocknr, num_blocks, zero, curr_cpu,
                    inode->nsocket);

    inode->nsocket = sufs_get_nsocket(sb, inode);

    return allocated;
}

/*
 * Allocate index blocks; blocks to hold the indexing data of a file.
 * The offset for the allocated block comes back in
 * blocknr.  Return the number of blocks allocated.
 */
static int sufs_do_new_index_blocks(struct super_block *sb,
                    struct sufs_inode *inode,
                    unsigned long *blocknr, unsigned int num,
                    int zero, int curr_cpu)
{
    int allocated, node;

    /*
   * This is one policy, use the pm_node of the curr_cpu. The other policy
   * may be record the numa of the cpu that creates the inode and just use
   * that
   */

    node = cpu_to_node(curr_cpu);

    allocated = sufs_new_blocks(sb, blocknr, num, zero, curr_cpu, node);

    return allocated;
}


/*
 * allocate a data block for inode and return it's absolute blocknr.
 * Zeroes out the block if zero set. Increments inode->i_blocks.
 */
int sufs_new_data_blocks(struct sufs_super_block * sb, struct sufs_inode *pi,
             unsigned long *blocknr, int zero)
{
    unsigned int data_bits = blk_type_to_shift_pmfs[pi->i_blk_type];

    int allocated = sufs_do_new_data_blocks(sb, pi, blocknr, 1, zero,
                        smp_processor_id());

    if (allocated > 0) {
        sufs_memunlock_inode(sb, pi);
        le64_add_cpu(&pi->i_blocks,
                 (1 << (data_bits - sb->s_blocksize_bits)));
        sufs_memlock_inode(sb, pi);
    }

    return allocated > 0 ? 0 : -1;
}

int sufs_new_index_blocks(struct sufs_super_block * sb,
        struct sufs_inode *inode, unsigned long *blocknr, int zero)
{
    int allocated = sufs_do_new_index_blocks(sb, inode, blocknr, 1, zero,
                         smp_processor_id());

    return allocated > 0 ? 0 : -1;
}


#endif
