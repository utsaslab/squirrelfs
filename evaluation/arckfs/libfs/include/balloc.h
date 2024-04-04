#ifndef SUFS_LIBFS_BALLOC_H_
#define SUFS_LIBFS_BALLOC_H_

#include <pthread.h>
#include <stdatomic.h>

#include "../../include/libfs_config.h"
#include "bitmap.h"
#include "rb.h"
#include "super.h"
#include "mnode.h"

struct sufs_libfs_kfs_blocks
{
    unsigned long start, size;
    struct sufs_libfs_kfs_blocks * next;
};

struct sufs_libfs_free_list
{
     pthread_spinlock_t s_lock;

     struct rb_root block_free_tree;
     struct sufs_libfs_range_node *first_node; // lowest address free range
     struct sufs_libfs_range_node *last_node; // highest address free range

     /*
      * Start and end of allocatable range, inclusive. Excludes csum and
      * parity blocks.
      */
     unsigned long block_start;
     unsigned long block_end;

     unsigned long num_free_blocks;

     /* How many nodes in the rb tree? */
     unsigned long num_blocknode;

     /* TODO: Think of the cache line break */

};

extern atomic_char * sufs_libfs_block_own_map;

extern int sufs_libfs_alloc_cpu;
extern int sufs_libfs_alloc_numa;
extern unsigned long sufs_libfs_init_alloc_size;
extern int sufs_alloc_pin_cpu;


static inline unsigned long
sufs_libfs_block_to_virt_addr(unsigned long block)
{
    return ((block << SUFS_PAGE_SHIFT) + sufs_libfs_sb.start_addr);
}

static inline unsigned long
sufs_libfs_virt_addr_to_block(unsigned long vaddr)
{
    return ((vaddr - sufs_libfs_sb.start_addr) >> SUFS_PAGE_SHIFT);
}

static inline unsigned long
sufs_libfs_offset_to_block(unsigned long offset)
{
    return (offset >> SUFS_PAGE_SHIFT);
}

static inline unsigned long
sufs_libfs_block_to_offset(unsigned long block)
{
    return (block << SUFS_PAGE_SHIFT);
}

static inline unsigned long
sufs_libfs_is_block_owned(unsigned long block)
{
    return sufs_libfs_bm_test_bit(sufs_libfs_block_own_map, block);
}

static inline void
sufs_libfs_block_clear_owned(unsigned long block)
{
    sufs_libfs_bm_clear_bit(sufs_libfs_block_own_map, block);
}
static inline void
sufs_libfs_block_set_owned(unsigned long block)
{
    sufs_libfs_bm_set_bit(sufs_libfs_block_own_map, block);
}

static inline struct sufs_libfs_free_list* sufs_libfs_get_free_list(
        struct sufs_libfs_super_block *sb, int cpu, int pm_node)
{
    // return &sb->free_lists[cpu * sb->pm_nodes + pm_node];
    return &sb->free_lists[cpu * sb->pm_nodes];
}

/* Which pm_node this block belongs to */
static inline int sufs_libfs_block_to_pm_node(struct sufs_libfs_super_block *sb,
        int blocknr)
{
    int i = 0;

    for (i = 0; i < sb->pm_nodes; i++)
    {
        if (blocknr >= sb->pm_node_info[i].start_block
                && blocknr <= sb->pm_node_info[i].end_block)
        {
            break;
        }
    }

    if (i == sb->pm_nodes)
    {
        printf("Cannot map blocknr: %d\n to pm_node", blocknr);
    }

    return i;
}

void sufs_libfs_alloc_block_free_lists(struct sufs_libfs_super_block *sb);

void sufs_libfs_delete_block_free_lists(struct sufs_libfs_super_block *sb);

void sufs_libfs_init_block_free_list(struct sufs_libfs_super_block *sb,
        int recovery);

int sufs_libfs_free_blocks(struct sufs_libfs_super_block *sb,
        unsigned long blocknr, unsigned long num_blocks);

int sufs_libfs_new_blocks(struct sufs_libfs_super_block *sb,
        unsigned long *blocknr, unsigned long num_blocks, int zero, int cpu,
        int pm_node);

int sufs_libfs_free_data_blocks(struct sufs_libfs_super_block * sb,
        unsigned long blocknr, int num);

int sufs_libfs_new_file_data_blocks(struct sufs_libfs_super_block * sb,
        struct sufs_libfs_mnode *mnode, unsigned long *blocknr, int count,
        int zero);

int sufs_libfs_new_dir_data_blocks(struct sufs_libfs_super_block * sb,
        struct sufs_libfs_mnode *mnode, unsigned long *blocknr, int count,
        int zero);

int sufs_libfs_new_index_blocks(struct sufs_libfs_super_block * sb,
        struct sufs_libfs_mnode *mnode, unsigned long *blocknr, int zero);

int sufs_libfs_free_index_block(struct sufs_libfs_super_block * sb,
        unsigned long blocknr);

unsigned long sufs_libfs_count_free_blocks(struct sufs_libfs_super_block *sb);

#endif
