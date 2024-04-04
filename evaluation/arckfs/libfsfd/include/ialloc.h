#ifndef SUFS_LIBFS_IALLOC_H_
#define SUFS_LIBFS_IALLOC_H_

#include <pthread.h>

#include "../../include/libfs_config.h"
#include "super.h"
#include "balloc.h"

/* List of inodes obtained from kfs */
struct sufs_libfs_kfs_inodes
{
   int ino, num;
   struct sufs_libfs_kfs_inodes * next;
};

struct sufs_libfs_inode_free_item
{
    int ino;
    struct sufs_libfs_inode_free_item * next;
};

struct sufs_libfs_inode_free_list
{
    struct sufs_libfs_inode_free_item * free_inode_head;
    struct sufs_libfs_kfs_inodes * free_kfs_head;
    pthread_spinlock_t lock;
};

extern atomic_char * sufs_libfs_inode_alloc_map;

static inline unsigned long
sufs_libfs_is_inode_allocated(int inode)
{
    return sufs_libfs_bm_test_bit(sufs_libfs_inode_alloc_map, inode);
}

static inline void
sufs_libfs_inode_clear_allocated(int inode)
{
    sufs_libfs_bm_clear_bit(sufs_libfs_inode_alloc_map, inode);
}
static inline void
sufs_libfs_inode_set_allocated(int inode)
{
    sufs_libfs_bm_set_bit(sufs_libfs_inode_alloc_map, inode);
}

void sufs_libfs_alloc_inode_free_lists(struct sufs_libfs_super_block *sb);

void sufs_libfs_free_inode_free_lists(struct sufs_libfs_super_block *sb);

int sufs_libfs_init_inode_free_lists(struct sufs_libfs_super_block *sb);

int sufs_libfs_free_inode(struct sufs_libfs_super_block * sb, int ino);

int sufs_libfs_new_inode(struct sufs_libfs_super_block * sb, int cpu);

#endif
