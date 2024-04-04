#ifndef SUFS_LIBFS_RB_H_
#define SUFS_LIBFS_RB_H_

#include <stdlib.h>

#include "../../include/libfs_config.h"
#include "rbtree.h"


/* Range node type */
enum node_type
{
    NODE_BLOCK = 1, NODE_INODE, NODE_DIR,
};

/* A node in the RB tree representing a range of pages */
struct sufs_libfs_range_node
{
    struct rb_node node;
    union
    {
        /* Block, inode */
        struct
        {
                unsigned long range_low;
                unsigned long range_high;
        };
        /* Dir node */
        struct
        {
                unsigned long hash;
                void *direntry;
        };
    };
};


static inline struct sufs_libfs_range_node*
sufs_libfs_alloc_range_node(void)
{
    struct sufs_libfs_range_node *p;

    p = (struct sufs_libfs_range_node*)
            calloc(1, sizeof(struct sufs_libfs_range_node));

    return p;
}

static inline void sufs_libfs_free_range_node(
        struct sufs_libfs_range_node *node)
{
    free(node);
}

static inline struct sufs_libfs_range_node*
sufs_libfs_alloc_blocknode(void)
{
    return sufs_libfs_alloc_range_node();
}

static inline void
sufs_libfs_free_blocknode(struct sufs_libfs_range_node *node)
{
    sufs_libfs_free_range_node(node);
}

static inline struct sufs_libfs_range_node*
sufs_libfs_alloc_inode_node(void)
{
    return sufs_libfs_alloc_range_node();
}

static inline void sufs_libfs_free_inode_node(
        struct sufs_libfs_range_node *node)
{
    sufs_libfs_free_range_node(node);
}

static inline int sufs_libfs_rbtree_compare_range_node(
        struct sufs_libfs_range_node *curr, unsigned long key,
        enum node_type type)
{
    if (type == NODE_DIR)
    {
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

int sufs_libfs_init_rangenode_cache(void);
void sufs_libfs_free_rangenode_cache(void);

int sufs_libfs_rbtree_find_range_node(struct rb_root *tree, unsigned long key,
        enum node_type type, struct sufs_libfs_range_node **ret_node);

int sufs_libfs_rbtree_insert_range_node(struct rb_root *tree,
        struct sufs_libfs_range_node *new_node, enum node_type type);

void sufs_libfs_rbtree_destroy_range_node_tree(struct rb_root *tree);

#endif
