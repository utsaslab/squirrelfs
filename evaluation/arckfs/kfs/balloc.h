#ifndef SUFS_KFS_BALLOC_H_
#define SUFS_KFS_BALLOC_H_

#include <linux/rbtree.h>
#include <linux/spinlock.h>

#include "../include/kfs_config.h"
#include "rbtree.h"
#include "super.h"

struct sufs_free_list {
    spinlock_t s_lock;

    struct rb_root block_free_tree;
    struct sufs_range_node *first_node; // lowest address free range
    struct sufs_range_node *last_node; // highest address free range

    /*
     * Start and end of allocatable range, inclusive. Excludes csum and
     * parity blocks.
     */
    unsigned long block_start;
    unsigned long block_end;

    unsigned long num_free_blocks;

    /* How many nodes in the rb tree? */
    unsigned long num_blocknode;

    u64 padding[8]; /* Cache line break */
};


static inline unsigned long
sufs_kfs_block_to_virt_addr(unsigned long block)
{
    return (block << PAGE_SHIFT) + sufs_sb.start_virt_addr;
}

static inline unsigned long
sufs_kfs_virt_addr_to_block(unsigned long addr)
{
    return (addr - sufs_sb.start_virt_addr) >> PAGE_SHIFT;
}

static inline unsigned long
sufs_kfs_offset_to_block(unsigned long offset)
{
    return (offset >> PAGE_SHIFT);
}

static inline unsigned long
sufs_kfs_block_to_offset(unsigned long block)
{
    return (block << PAGE_SHIFT);
}


static inline struct sufs_free_list * sufs_get_free_list(
        struct sufs_super_block *sb, int cpu, int pm_node)
{
    // return &sb->free_lists[cpu * sb->pm_nodes + pm_node];
    return &sb->free_lists[cpu * sb->pm_nodes];
}

/* Which pm_node this block belongs to */
static inline int sufs_block_to_pm_node(struct sufs_super_block *sb, int blocknr)
{
    int i = 0;

    for (i = 0; i < sb->pm_nodes; i++) {
        if (blocknr >= sb->pm_node_info[i].start_block &&
            blocknr <= sb->pm_node_info[i].end_block) {
            break;
        }
    }

    if (i == sb->pm_nodes)
    {
        printk("Cannot map blocknr: %d\n to pm_node", blocknr);
    }

    return i;
}

int sufs_alloc_block_free_lists(struct sufs_super_block * sb);

void sufs_delete_block_free_lists(struct sufs_super_block * sb);

void sufs_init_block_free_list(struct sufs_super_block * sb, int recovery);


int sufs_free_blocks(struct sufs_super_block * sb, unsigned long blocknr,
                unsigned long num_blocks);

int sufs_new_blocks(struct sufs_super_block * sb, unsigned long *blocknr,
               unsigned long num_blocks, int zero, int cpu,
               int pm_node);

unsigned long sufs_count_free_blocks(struct sufs_super_block * sb);

unsigned long sufs_sys_info_libfs(unsigned long arg);

int sufs_alloc_blocks_to_libfs(unsigned long uaddr);

int sufs_free_blocks_from_libfs(unsigned long uaddr);

#endif
