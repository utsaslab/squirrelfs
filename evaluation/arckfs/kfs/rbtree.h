#ifndef SUFS_KFS_RBTREE_H_
#define SUFS_KFS_RBTREE_H_

#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

#include "../include/kfs_config.h"

/* Range node type */
enum node_type {
    NODE_BLOCK = 1,
    NODE_INODE,
    NODE_DIR,
};

/* A node in the RB tree representing a range of pages */
struct sufs_range_node {
    struct rb_node node;
    union {
        /* Block, inode */
        struct {
            unsigned long range_low;
            unsigned long range_high;
        };
        /* Dir node */
        struct {
            unsigned long hash;
            void *direntry;
        };
    };
};


extern struct kmem_cache *sufs_range_node_cachep;



static inline struct sufs_range_node *
sufs_alloc_range_node(void)
{
    struct sufs_range_node *p;

    p = (struct sufs_range_node *)
            kmem_cache_zalloc(sufs_range_node_cachep, GFP_NOFS);

    return p;
}

static inline void
sufs_free_range_node(struct sufs_range_node *node)
{
    kmem_cache_free(sufs_range_node_cachep, node);
}


static inline struct sufs_range_node *
sufs_alloc_blocknode(void)
{
    return sufs_alloc_range_node();
}


static inline void
sufs_free_blocknode(struct sufs_range_node *node)
{
    sufs_free_range_node(node);
}

static inline struct sufs_range_node *
sufs_alloc_inode_node(void)
{
    return sufs_alloc_range_node();
}


static inline void
sufs_free_inode_node(struct sufs_range_node *node)
{
    sufs_free_range_node(node);
}

static inline int sufs_rbtree_compare_range_node(struct sufs_range_node *curr,
                         unsigned long key, enum node_type type)
{
    if (type == NODE_DIR) {
        if (key < curr->hash)
            return -1;
        if (key > curr->hash)
            return 1;
        return 0;
    }
    /* Block and inode */
    if (key < curr->range_low)
        return -1;
    if (key > curr->range_high)
        return 1;

    return 0;
}

int sufs_init_rangenode_cache(void);
void sufs_free_rangenode_cache(void);


int sufs_rbtree_find_range_node(struct rb_root *tree, unsigned long key,
        enum node_type type, struct sufs_range_node **ret_node);

int sufs_rbtree_insert_range_node(struct rb_root *tree,
                  struct sufs_range_node *new_node, enum node_type type);

void sufs_rbtree_destroy_range_node_tree(struct rb_root *tree);



#endif /* SUFS_KFS_BALLOC_H_ */
