#ifndef SUFS_LIBFS_RADIX_ARRAY_H_
#define SUFS_LIBFS_RADIX_ARRAY_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>

#include "../../include/libfs_config.h"

#define SUFS_LIBFS_RA_LOCK_BIT 2
#define SUFS_LIBFS_RA_TYPE_MASK (3 << 0)
#define SUFS_LIBFS_RA_LOCK_MASK (1 << SUFS_LIBFS_RA_LOCK_BIT)
#define SUFS_LIBFS_RA_MASK (SUFS_LIBFS_RA_TYPE_MASK | SUFS_LIBFS_RA_LOCK_MASK)

#define SUFS_LIBFS_RA_TYPE_NONE 0
#define SUFS_LIBFS_RA_TYPE_SET 1

/* need to think of the synchronization issue here */
struct sufs_libfs_ra_node
{
        unsigned long *child;
};

struct sufs_libfs_ra_node_ptr
{
        uintptr_t v;
};

struct sufs_libfs_radix_array
{
        unsigned int levels;

        size_t upper_bits;
        size_t leaf_bits;

        size_t upper_fanout;
        size_t leaf_fanout;

        size_t n;

        unsigned long root_;
};

static inline size_t sufs_libfs_ceil_log2_const(size_t x, bool exact)
{
    return (x == 0) ? (1 / x) :
           (x == 1) ?
                   (exact ? 0 : 1) :
                   1
                           + sufs_libfs_ceil_log2_const(x >> 1,
                                   ((x & 1) == 1) ? false : exact);
}

static inline size_t sufs_libfs_round_up_to_pow2_const(size_t x)
{
    return (size_t) 1 << sufs_libfs_ceil_log2_const(x, true);
}

static inline size_t sufs_libfs_log2_exact(size_t x, size_t accum)
{
    return (x == 0) ? (1 / x) : (x == 1) ? accum :
           ((x & 1) == 0) ? sufs_libfs_log2_exact(x >> 1, accum + 1) : ~0;
}

static inline size_t sufs_libfs_ra_key_shift(struct sufs_libfs_radix_array *ra,
        unsigned level)
{
    return level == 0 ? 0 : ra->leaf_bits + ((level - 1) * ra->upper_bits);
}

static inline size_t sufs_libfs_ra_key_mask(struct sufs_libfs_radix_array *ra,
        unsigned level)
{
    return level == 0 ? (ra->leaf_fanout - 1) : (ra->upper_fanout - 1);
}

static inline size_t sufs_libfs_ra_level_span(struct sufs_libfs_radix_array *ra,
        unsigned level)
{
    return (size_t) 1 << sufs_libfs_ra_key_shift(ra, level);
}

static inline unsigned int sufs_libfs_ra_num_levels(
        struct sufs_libfs_radix_array *ra, unsigned level)
{
    return sufs_libfs_ra_level_span(ra, level) >= ra->n ?
            level : sufs_libfs_ra_num_levels(ra, level + 1);
}

static inline struct sufs_libfs_ra_node*
sufs_libfs_ra_ptr_to_node(struct sufs_libfs_ra_node_ptr *ptr)
{
    return (struct sufs_libfs_ra_node*) (ptr->v & ~SUFS_LIBFS_RA_MASK);
}

static inline unsigned int sufs_libfs_ra_subkey(
        struct sufs_libfs_radix_array *ra, size_t k, unsigned level)
{
    return (k >> sufs_libfs_ra_key_shift(ra, level))
            & sufs_libfs_ra_key_mask(ra, level);
}

static inline unsigned int sufs_libfs_ra_get_type(
        struct sufs_libfs_ra_node_ptr *ptr)
{
    return (ptr->v & SUFS_LIBFS_RA_MASK);
}

static inline void sufs_libfs_free_radix_array(
        struct sufs_libfs_radix_array *ra)
{
    free((void*) ra->root_);
}

void sufs_libfs_init_radix_array(struct sufs_libfs_radix_array *ra,
        size_t t_size, size_t n, size_t node_bytes);

unsigned long sufs_libfs_radix_array_find(
        struct sufs_libfs_radix_array *ra, size_t index, int fill,
        unsigned long ps);

void sufs_libfs_radix_array_free_recursive(struct sufs_libfs_radix_array *ra,
        unsigned long addr, int level);

static inline void sufs_libfs_radix_array_fini(
        struct sufs_libfs_radix_array *ra)
{
    sufs_libfs_radix_array_free_recursive(ra, ra->root_, ra->levels);
}

#endif
