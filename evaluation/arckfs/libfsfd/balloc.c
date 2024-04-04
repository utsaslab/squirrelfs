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

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdatomic.h>

#include "../include/libfs_config.h"
#include "../include/ring_buffer.h"

#include "balloc.h"
#include "rb.h"
#include "super.h"
#include "cmd.h"
#include "util.h"
#include "delegation.h"
#include "journal.h"
#include "tls.h"

/*
 * a bit map indicates whether the block is zeroed or not
 * set: not zeroed
 */
static atomic_char * sufs_libfs_block_zero_map = NULL;

/* a bit map indicates whether the block is owned or not
 * set: owned
 * See the comments on ialloc.c to see how this map should be maintained
 */

atomic_char * sufs_libfs_block_own_map = NULL;

int sufs_libfs_alloc_cpu = 0, sufs_libfs_alloc_numa = 0, sufs_alloc_pin_cpu = 0;
unsigned long sufs_libfs_init_alloc_size = 0;

void sufs_libfs_init_balloc_map(struct sufs_libfs_super_block * sb)
{
    int i = 0;
    long tot_block = 0;
    for (i = 0; i < sb->pm_nodes; i++)
    {
        tot_block += (sb->pm_node_info[i].end_block -
                sb->pm_node_info[i].start_block + 1);
    }

    sufs_libfs_block_zero_map = calloc(1, tot_block / sizeof(char));
    sufs_libfs_block_own_map = calloc(1, tot_block / sizeof(char));

    if (!sufs_libfs_block_zero_map || !sufs_libfs_block_own_map)
    {
        fprintf(stderr, "Cannot allocate block map!\n");
        abort();
    }
}

static int sufs_libfs_insert_blocktree(struct rb_root *tree,
        struct sufs_libfs_range_node *new_node)
{
    int ret = 0;

    ret = sufs_libfs_rbtree_insert_range_node(tree, new_node, NODE_BLOCK);
    if (ret)
        printf("ERROR: %s failed %d\n", __func__, ret);

    return ret;
}

/* init */

void sufs_libfs_alloc_block_free_lists(struct sufs_libfs_super_block *sb)
{
    struct sufs_libfs_free_list *free_list = NULL;
    int i = 0, j = 0;

    int cpus = sb->cpus_per_socket * sb->sockets;

    sb->free_lists = calloc(cpus * sb->pm_nodes,
            sizeof(struct sufs_libfs_free_list));

    if (!sb->free_lists)
    {
        fprintf(stderr, "Cannot allocate free_lists!\n");
        abort();
    }

    sufs_libfs_init_balloc_map(sb);

    for (i = 0; i < cpus; i++)
    {
        for (j = 0; j < sb->pm_nodes; j++)
        {
            free_list = sufs_libfs_get_free_list(sb, i, j);
            free_list->block_free_tree = RB_ROOT;
            pthread_spin_init(&free_list->s_lock, PTHREAD_PROCESS_SHARED);
        }
    }
}

static void sufs_libfs_free_one_block_free_list(
        struct sufs_libfs_free_list *free_list)
{
    struct rb_node * temp = &(free_list->first_node->node);
    struct sufs_libfs_range_node *curr = NULL;

    while (temp)
    {
        curr = container_of(temp, struct sufs_libfs_range_node, node);
        sufs_libfs_cmd_free_blocks(curr->range_low,
                curr->range_high - curr->range_low + 1);

        temp = rb_next(temp);
    }
}

void sufs_libfs_delete_block_free_lists(struct sufs_libfs_super_block *sb)
{
    struct sufs_libfs_free_list *free_list = NULL;
    int i = 0, j = 0;

    int cpus = sb->cpus_per_socket * sb->sockets;

    for (i = 0; i < cpus; i++)
    {
        for (j = 0; j < sb->pm_nodes; j++)
        {
            free_list = sufs_libfs_get_free_list(sb, i, j);
            sufs_libfs_free_one_block_free_list(free_list);
        }
    }

    /* Each tree is freed in save_blocknode_mappings */
    if (sb->free_lists)
        free(sb->free_lists);

    sb->free_lists = NULL;
}

/*
 * Initialize a free list.  Each CPU gets an equal share of the block space to
 * manage.
 */

static void sufs_libfs_init_free_list(struct sufs_libfs_super_block *sb,
        struct sufs_libfs_free_list *free_list, int cpu, int pm_node,
        unsigned long num)
{
    unsigned long block = 0;

    int ret = 0;

    ret = sufs_libfs_cmd_alloc_blocks(&block, &num, cpu, pm_node);

    if (ret < 0)
    {
        fprintf(stderr, "alloc block: num %ld cpu %d pm_node %d failed\n", num,
                cpu, pm_node);
        return;
    }

    free_list->block_start = block;
    free_list->block_end = block + num - 1;
}

static int sufs_libfs_env_alloc(struct sufs_libfs_super_block * sb,
        int cpu, int numa)
{
    if (cpu < sufs_libfs_alloc_cpu)
    {
        if (sufs_libfs_alloc_numa == -1)
        {
            return (cpu / sb->cpus_per_socket == numa);
        }
        else
        {
            return (numa < sufs_libfs_alloc_numa);
        }
    }

    return 0;
}

void sufs_libfs_init_block_free_list(struct sufs_libfs_super_block *sb,
        int recovery)
{
    struct rb_root *tree = NULL;
    struct sufs_libfs_range_node *blknode = NULL;
    struct sufs_libfs_free_list *free_list = NULL;
    int i = 0, j = 0, cpus = 0;
    int ret = 0;

    cpus = sb->cpus_per_socket * sb->sockets;

    /* Divide the block range among per-CPU free lists */
    for (i = 0; i < cpus; i++)
    {
        for (j = 0; j < sb->pm_nodes; j++)
        {
            unsigned long block_num = SUFS_LIBFS_INIT_BLOCK_CHUNK;
            free_list = sufs_libfs_get_free_list(sb, i, j);
            tree = &(free_list->block_free_tree);

            if (sufs_libfs_env_alloc(sb, i, j))
            {
                if (sufs_alloc_pin_cpu)
                {
                    sufs_libfs_pin_to_core(i);
                }
                block_num = sufs_libfs_init_alloc_size;
            }

            sufs_libfs_init_free_list(sb, free_list, i, j,
                    block_num);

            /* For recovery, update these fields later */
            if (recovery == 0)
            {
                free_list->num_free_blocks = free_list->block_end
                        - free_list->block_start + 1;

                blknode = sufs_libfs_alloc_blocknode();

                if (blknode == NULL)
                {
                    fprintf(stderr, "Cannot alloc blknode!\n");
                    abort();
                }

                blknode->range_low = free_list->block_start;
                blknode->range_high = free_list->block_end;

                ret = sufs_libfs_insert_blocktree(tree, blknode);
                if (ret)
                {
                    printf("%s failed\n", __func__);
                    sufs_libfs_free_blocknode(blknode);
                    return;
                }
                free_list->first_node = blknode;
                free_list->last_node = blknode;
                free_list->num_blocknode = 1;
            }

#if 0
            printf("%s: free list, addr: %lx, cpu %d, pm_node %d,  block "
                    "start %lu, end %lu, "
                    "%lu free blocks\n", __func__, (unsigned long) free_list, i,
                    j, free_list->block_start, free_list->block_end,
                    free_list->num_free_blocks);
#endif
        }
    }
}

/* Which cpu or pm_node this block belongs to */
static void sufs_libfs_block_to_cpu_pm_nodes(struct sufs_libfs_super_block *sb,
        int blocknr, int *cpu, int *pm_nodes)
{
    int cpu_tmp = 0;
    unsigned long size = 0;
    int tmp_pm_nodes = 0;

    int cpus = sb->cpus_per_socket * sb->sockets;

    tmp_pm_nodes = sufs_libfs_block_to_pm_node(sb, blocknr);

    if (pm_nodes)
        *pm_nodes = tmp_pm_nodes;

    size = sb->pm_node_info[tmp_pm_nodes].end_block
            - sb->pm_node_info[tmp_pm_nodes].start_block + 1;

    cpu_tmp = (blocknr - sb->pm_node_info[tmp_pm_nodes].start_block)
            / (size / cpus);

    /* The remainder of the last cpu */
    if (cpu_tmp >= cpus)
        cpu_tmp = cpus - 1;

    if (cpu)
        (*cpu) = cpu_tmp;

    return;
}

/* Used for both block free tree and inode inuse tree */
static int sufs_libfs_find_free_slot(struct rb_root *tree,
        unsigned long range_low, unsigned long range_high,
        struct sufs_libfs_range_node **prev,
        struct sufs_libfs_range_node **next)
{
    struct sufs_libfs_range_node *ret_node = NULL;
    struct rb_node *tmp = NULL;
    int ret = 0;

    ret = sufs_libfs_rbtree_find_range_node(tree, range_low, NODE_BLOCK,
            &ret_node);
    if (ret)
    {
        printf("%s ERROR: %lu - %lu already in free list\n", __func__,
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
            *next = container_of(tmp, struct sufs_libfs_range_node, node);
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
            *prev = container_of(tmp, struct sufs_libfs_range_node, node);
        }
        else
        {
            *prev = NULL;
        }
    }
    else
    {
        printf("%s ERROR: %lu - %lu overlaps with existing "
                "node %lu - %lu\n", __func__, range_low, range_high,
                ret_node->range_low, ret_node->range_high);
        return -EINVAL;
    }

    return 0;
}

int __sufs_libfs_free_blocks(struct sufs_libfs_super_block *sb,
        struct sufs_libfs_free_list *free_list, unsigned long blocknr,
        unsigned long num_blocks)
{

    struct rb_root *tree = NULL;
    unsigned long block_low = 0;
    unsigned long block_high = 0;
    struct sufs_libfs_range_node *prev = NULL;
    struct sufs_libfs_range_node *next = NULL;
    struct sufs_libfs_range_node *curr_node = NULL;
    int new_node_used = 0;
    int ret = 0, i = 0;


    /* Pre-allocate blocknode */
    curr_node = sufs_libfs_alloc_blocknode();
    if (curr_node == NULL)
    {
        /* returning without freeing the block*/
        return -ENOMEM;
    }

    tree = &(free_list->block_free_tree);

    block_low = blocknr;
    block_high = blocknr + num_blocks - 1;

    ret = sufs_libfs_find_free_slot(tree, block_low, block_high, &prev, &next);

    if (ret)
    {
        printf("%s: find free slot fail: %d\n", __func__, ret);
        goto out;
    }

    if (prev && next && (block_low == prev->range_high + 1)
            && (block_high + 1 == next->range_low))
    {
        /* fits the hole */
        rb_erase(&next->node, tree);
        free_list->num_blocknode--;
        prev->range_high = next->range_high;
        if (free_list->last_node == next)
            free_list->last_node = prev;
        sufs_libfs_free_blocknode(next);
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
    ret = sufs_libfs_insert_blocktree(tree, curr_node);
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

    for (i = block_low; i <= block_high; i++)
    {
        sufs_libfs_bm_set_bit(sufs_libfs_block_zero_map, i);
        sufs_libfs_block_clear_owned(i);
    }

out:

    if (new_node_used == 0)
        sufs_libfs_free_blocknode(curr_node);

    return ret;
}

/*
 * This has the implicit assumption that the freed block chunk only belongs to
 * one CPU pool
 */
int sufs_libfs_free_blocks(struct sufs_libfs_super_block *sb,
        unsigned long blocknr, unsigned long num_blocks)
{
    struct sufs_libfs_free_list *free_list = NULL;
    int cpuid = 0, pm_node = 0;
    int ret = 0;

    if (num_blocks <= 0)
    {
        printf("%s ERROR: free %lu\n", __func__, num_blocks);
        return -EINVAL;
    }

    sufs_libfs_block_to_cpu_pm_nodes(sb, blocknr, &cpuid, &pm_node);

    free_list = sufs_libfs_get_free_list(sb, cpuid, pm_node);

    pthread_spin_lock(&free_list->s_lock);

    ret = __sufs_libfs_free_blocks(sb, free_list, blocknr, num_blocks);

    pthread_spin_unlock(&free_list->s_lock);

    return ret;
}

static int not_enough_blocks(struct sufs_libfs_free_list *free_list,
        unsigned long num_blocks)
{
    struct sufs_libfs_range_node *first = free_list->first_node;
    struct sufs_libfs_range_node *last = free_list->last_node;

    /*
     * free_list->num_free_blocks / free_list->num_blocknode is used to handle
     * fragmentation within blocknodes
     */
    if (!first || !last
            || free_list->num_free_blocks / free_list->num_blocknode
                    < num_blocks)
    {
#if 0
        printf("%s: num_free_blocks=%ld; num_blocks=%ld; "
                "first=0x%p; last=0x%p\n", __func__, free_list->num_free_blocks,
                num_blocks, first, last);
#endif
        return 1;
    }

    return 0;
}

/* Return how many blocks allocated */
static long sufs_libfs_alloc_blocks_in_free_list(
        struct sufs_libfs_super_block *sb,
        struct sufs_libfs_free_list *free_list, unsigned long num_blocks,
        unsigned long *new_blocknr)
{
    struct rb_root *tree = NULL;
    struct sufs_libfs_range_node *curr = NULL, *next = NULL, *prev = NULL;
    struct rb_node *temp = NULL, *next_node = NULL, *prev_node = NULL;
    unsigned long curr_blocks = 0;
    bool found = 0;

    unsigned long step = 0;

    if (!free_list->first_node || free_list->num_free_blocks == 0)
    {
        printf("%s: Can't alloc. free_list->first_node=0x%p "
                "free_list->num_free_blocks = %lu\n", __func__,
                free_list->first_node, free_list->num_free_blocks);
        return -ENOSPC;
    }

    tree = &(free_list->block_free_tree);
    temp = &(free_list->first_node->node);

    /* always use the unaligned approach */
    while (temp)
    {
        step++;
        curr = container_of(temp, struct sufs_libfs_range_node, node);

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
                    next = container_of(next_node, struct sufs_libfs_range_node,
                            node);
                }

                free_list->first_node = next;
            }

            if (curr == free_list->last_node)
            {
                prev_node = rb_prev(temp);
                if (prev_node)
                {
                    prev = container_of(prev_node, struct sufs_libfs_range_node,
                            node);
                }

                free_list->last_node = prev;
            }

            rb_erase(&curr->node, tree);
            free_list->num_blocknode--;
            num_blocks = curr_blocks;
            *new_blocknr = curr->range_low;
            sufs_libfs_free_blocknode(curr);
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
        printf("%s: free list has %lu free blocks, "
                "but allocated %lu blocks?\n", __func__,
                free_list->num_free_blocks, num_blocks);
        return -ENOSPC;
    }

    if (found == 1)
    {
        free_list->num_free_blocks -= num_blocks;
    }
    else
    {
        printf("%s: Can't alloc.  found = %d\n", __func__, found);
        return -ENOSPC;
    }

    return num_blocks;
}

static void sufs_libfs_alloc_zero_blocks(unsigned long start_block,
        unsigned long block_counts)
{
    unsigned long i = 0, zeroed_blocks = 0, start_zeroed_blocks = 0;
    int delegated = 0, cpt_idx = 0, index = 0;

    long issued_cnt[SUFS_PM_MAX_INS];
    struct sufs_notifyer * completed_cnt = NULL;

    if (sufs_libfs_delegation)
    {
        index = sufs_libfs_tls_my_index();

        memset(issued_cnt, 0, sizeof(long) * SUFS_PM_MAX_INS);

        completed_cnt = sufs_libfs_tls_data[index].cpt_cnt;

        if (completed_cnt == NULL)
        {
            completed_cnt = sufs_libfs_get_completed_cnt(index);
        }

#if 0
        printf("level is %d\n", sufs_libfs_cpt_level);
#endif

        completed_cnt = (struct sufs_notifyer * )((unsigned long) completed_cnt +
                1 * sizeof(struct sufs_notifyer) * SUFS_PM_MAX_INS);

        memset(completed_cnt, 0,
               sizeof(struct sufs_notifyer) * SUFS_PM_MAX_INS);

        cpt_idx = sufs_libfs_tls_data[index].cpt_idx;
    }

    for (i = start_block; i < start_block + block_counts; i++)
    {
        if (sufs_libfs_bm_test_bit(sufs_libfs_block_zero_map, i))
        {
            if (start_zeroed_blocks == 0)
                start_zeroed_blocks = i;

            zeroed_blocks++;
        }
        else if (start_zeroed_blocks != 0)
        {
            if ((!sufs_libfs_delegation) ||
                    (zeroed_blocks * SUFS_PAGE_SIZE < SUFS_WRITE_DELEGATION_LIMIT))
            {
                unsigned long vaddr = sufs_libfs_block_to_virt_addr(start_zeroed_blocks);
#if 0
                printf("memset start_zeroed_blocks: %ld, zeroed_blocks: %ld\n",
                        start_zeroed_blocks, zeroed_blocks);
#endif
                memset((void *) vaddr, 0, zeroed_blocks * SUFS_PAGE_SIZE);

                sufs_libfs_clwb_buffer((void *) vaddr, zeroed_blocks * SUFS_PAGE_SIZE);
            }
            else
            {
                delegated = 1;

                sufs_libfs_do_write_delegation(&sufs_libfs_sb,
                    (unsigned long) 0,
                    sufs_libfs_block_to_offset(start_zeroed_blocks),
                    zeroed_blocks * SUFS_PAGE_SIZE,
                    1, 1, 0, issued_cnt,
                    cpt_idx, 2, index);
            }

            start_zeroed_blocks = 0;
            zeroed_blocks = 0;
        }

    }

    if (start_zeroed_blocks != 0)
    {
        if ((!sufs_libfs_delegation) ||
                (zeroed_blocks * SUFS_PAGE_SIZE < SUFS_WRITE_DELEGATION_LIMIT))
        {
            unsigned long vaddr = sufs_libfs_block_to_virt_addr(start_zeroed_blocks);
#if 0
                printf("memset start_zeroed_blocks: %ld, zeroed_blocks: %ld\n",
                        start_zeroed_blocks, zeroed_blocks);
#endif
            memset((void *) vaddr, 0, zeroed_blocks * SUFS_PAGE_SIZE);
            sufs_libfs_clwb_buffer((void *) vaddr, zeroed_blocks * SUFS_PAGE_SIZE);
        }
        else
        {
            delegated = 1;

            sufs_libfs_do_write_delegation(&sufs_libfs_sb,
                (unsigned long) 0,
                sufs_libfs_block_to_offset(start_zeroed_blocks),
                zeroed_blocks * SUFS_PAGE_SIZE,
                1, 1, 0, issued_cnt,
                cpt_idx, 2, index);
        }
    }

    if (delegated)
    {
        sufs_libfs_complete_delegation(&sufs_libfs_sb, issued_cnt, completed_cnt);
    }

    /* sufs_libfs_sfence(); */
}

int sufs_libfs_new_blocks(struct sufs_libfs_super_block *sb,
        unsigned long *blocknr, unsigned long num_blocks, int zero, int cpu,
        int pm_node)
{
    struct sufs_libfs_free_list *free_list = NULL;
    unsigned long new_blocknr = 0;
    long ret_blocks = 0;
    int i = 0;

    if (num_blocks == 0)
    {
        printf("%s: num_blocks == 0\n", __func__);
        return -EINVAL;
    }

    free_list = sufs_libfs_get_free_list(sb, cpu, pm_node);
    pthread_spin_lock(&free_list->s_lock);

    if (not_enough_blocks(free_list, num_blocks))
    {
        unsigned long block = 0, num = SUFS_LIBFS_EXTRA_BLOCK_CHUNK, i = 0;

        if (num_blocks > num)
        {
            num = (num_blocks / SUFS_LIBFS_EXTRA_BLOCK_CHUNK + 1)
                    * SUFS_LIBFS_EXTRA_BLOCK_CHUNK;
        }

#if 0
        printf("%s: cpu %d, pm_node: %d, free_blocks %lu, required %lu, "
                "blocknode %lu, requested_block: %ld\n", __func__, cpu, pm_node,
                free_list->num_free_blocks, num_blocks,
                free_list->num_blocknode, num);
#endif

        if (sufs_libfs_cmd_alloc_blocks(&block, &num, cpu, pm_node) < 0)
        {
            fprintf(stderr, "alloc block: num %ld cpu %d pm_node %d failed\n",
                    num, cpu, pm_node);

            pthread_spin_unlock(&free_list->s_lock);

            return -ENOMEM;
        }

        for (i = block; i < block + num; i++)
        {
            sufs_libfs_bm_clear_bit(sufs_libfs_block_zero_map, i);
        }

        __sufs_libfs_free_blocks(sb, free_list, block, num);

    }

    ret_blocks = sufs_libfs_alloc_blocks_in_free_list(sb, free_list, num_blocks,
            &new_blocknr);

    pthread_spin_unlock(&free_list->s_lock);

    if (ret_blocks <= 0 || new_blocknr == 0)
    {
        printf("%s: not able to allocate %lu blocks. "
                "ret_blocks=%ld; new_blocknr=%lu\n", __func__, num_blocks,
                ret_blocks, new_blocknr);
        return -ENOSPC;
    }


    if (zero)
        sufs_libfs_alloc_zero_blocks(new_blocknr, ret_blocks);


    for (i = new_blocknr; i < new_blocknr + ret_blocks; i++)
    {
        sufs_libfs_block_set_owned(i);
    }

    if (blocknr)
        *blocknr = new_blocknr;

    return ret_blocks;
}

int sufs_libfs_free_data_blocks(struct sufs_libfs_super_block * sb,
        unsigned long blocknr, int num)
{
    int ret;

    if (blocknr == 0)
    {
        fprintf(stderr, "%s: ERROR: %lu, %d\n", __func__, blocknr, num);
        return -EINVAL;
    }

    ret = sufs_libfs_free_blocks(sb, blocknr, num);

    return ret;
}

/*
 * Allocate data blocks; blocks to hold file data
 * The offset for the allocated block comes back in
 * blocknr.  Return the number of blocks allocated.
 */

static int inline sufs_libfs_get_next_node(struct sufs_libfs_super_block *sb,
        struct sufs_libfs_mnode * mnode)
{
    return ((mnode->node + 1) % sb->pm_nodes);
}

static int sufs_libfs_do_new_file_data_blocks(struct sufs_libfs_super_block *sb,
                   struct sufs_libfs_mnode *mnode,
                   unsigned long *blocknr, unsigned int num,
                   int zero, int curr_cpu)
{
    int allocated;

    allocated = sufs_libfs_new_blocks(sb, blocknr, num, zero, curr_cpu,
            mnode->node);

    mnode->node= sufs_libfs_get_next_node(sb, mnode);

    return allocated;
}

static int sufs_libfs_do_new_dir_data_blocks(struct sufs_libfs_super_block *sb,
                   struct sufs_libfs_mnode *mnode,
                   unsigned long *blocknr, unsigned int num,
                   int zero, int curr_cpu)
{
    int allocated;

    allocated = sufs_libfs_new_blocks(sb, blocknr, num, zero, curr_cpu,
            sufs_libfs_current_node());

    return allocated;
}

int sufs_libfs_new_file_data_blocks(struct sufs_libfs_super_block * sb,
        struct sufs_libfs_mnode *mnode, unsigned long *blocknr, int count,
        int zero)
{
    int cpu = 0;

    if (sufs_libfs_getcpu(&cpu, NULL) < 0)
    {
        fprintf(stderr, "sufs_getcpu failed!\n");
        abort();
    }

    int allocated = sufs_libfs_do_new_file_data_blocks(sb, mnode, blocknr, count,
            zero, cpu);

    return allocated > 0 ? 0 : -1;
}

int sufs_libfs_new_dir_data_blocks(struct sufs_libfs_super_block * sb,
        struct sufs_libfs_mnode *mnode, unsigned long *blocknr, int count,
        int zero)
{
    int cpu = 0;

    if (sufs_libfs_getcpu(&cpu, NULL) < 0)
    {
        fprintf(stderr, "sufs_getcpu failed!\n");
        abort();
    }

    int allocated = sufs_libfs_do_new_dir_data_blocks(sb, mnode, blocknr, count,
            zero, cpu);

    return allocated > 0 ? 0 : -1;
}



static int sufs_libfs_do_new_index_blocks(struct sufs_libfs_super_block *sb,
                    struct sufs_libfs_mnode *mnode,
                    unsigned long *blocknr, unsigned int num,
                    int zero)
{
    int cpu = 0, numa = 0;
    /*
   * This is one policy, use the pm_node of the curr_cpu. The other policy
   * may be record the numa of the cpu that creates the inode and just use
   * that
   */

    if (sufs_libfs_getcpu(&cpu, &numa) < 0)
    {
        fprintf(stderr, "sufs_getcpu failed!\n");
        abort();
    }

    return sufs_libfs_new_blocks(sb, blocknr, num, zero, cpu, numa);

}

int sufs_libfs_new_index_blocks(struct sufs_libfs_super_block * sb,
        struct sufs_libfs_mnode *mnode, unsigned long *blocknr, int zero)
{
    int allocated = sufs_libfs_do_new_index_blocks(sb, mnode, blocknr, 1, zero);

    return allocated > 0 ? 0 : -1;
}

int sufs_libfs_free_index_block(struct sufs_libfs_super_block * sb,
        unsigned long blocknr)
{
    int ret;

    if (blocknr == 0)
    {
        fprintf(stderr, "%s: ERROR: %lu\n", __func__, blocknr);
        return -EINVAL;
    }

    ret = sufs_libfs_free_blocks(sb, blocknr, 1);

    return ret;
}

unsigned long sufs_libfs_count_free_blocks(struct sufs_libfs_super_block *sb)
{
    struct sufs_libfs_free_list *free_list = NULL;
    unsigned long num_free_blocks = 0;
    int i = 0, j = 0, cpus = 0;

    cpus = sb->cpus_per_socket * sb->sockets;

    for (i = 0; i < cpus; i++)
    {
        for (j = 0; j < sb->pm_nodes; j++)
        {
            free_list = sufs_libfs_get_free_list(sb, i, j);
            num_free_blocks += free_list->num_free_blocks;
        }

    }

    return num_free_blocks;
}
