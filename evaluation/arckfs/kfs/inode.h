#ifndef SUFS_KFS_INODE_H_
#define SUFS_KFS_INODE_H_

#include <linux/rbtree.h>
#include <linux/spinlock.h>

#include "../include/kfs_config.h"
#include "lease.h"
#include "rbtree.h"
#include "super.h"

/* Inode allocator */
struct sufs_inode_free_list {
    spinlock_t      lock;

    struct rb_root         inode_free_tree;
    struct sufs_range_node *first_node;
    struct sufs_range_node *last_node;

    int inode_start;
    int inode_end;

    /* How many nodes in the rb tree? */
    int num_inode_node;

    int num_free_inodes;
    /* TODO: reconsider Cache line break */
};



static inline struct sufs_shadow_inode * sufs_find_sinode(unsigned int ino_num)
{
    if (ino_num < 0 || ino_num > SUFS_MAX_INODE_NUM)
        return NULL;

    return (sufs_sb.sinode_start + ino_num);
}

int sufs_alloc_inode_free_list(struct sufs_super_block * sb);

void sufs_free_inode_free_list(struct sufs_super_block * sb);

int sufs_init_inode_free_list(struct sufs_super_block *sb);

int sufs_free_inodes(struct sufs_super_block * sb, int ino,
                int num);

int sufs_new_inodes(struct sufs_super_block * sb, int *inode_nr,
               int num_inodes, int zero, int cpu);

int sufs_alloc_inode_to_libfs(unsigned long uaddr);

int sufs_free_inode_from_libfs(unsigned long uaddr);

int sufs_kfs_set_inode(int ino, char type, unsigned int mode,
        unsigned int uid, unsigned int gid, unsigned long index_offset);

#endif
