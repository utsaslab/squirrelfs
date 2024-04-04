#ifndef SUFS_LIBFS_CHAINHASH_H_
#define SUFS_LIBFS_CHAINHASH_H_

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "../../include/libfs_config.h"
#include "compiler.h"
#include "types.h"

struct sufs_libfs_ch_item
{
        struct sufs_libfs_ch_item *next;

        char *key;
        unsigned long val;
        unsigned long val2;
};

struct sufs_libfs_ch_bucket
{
        pthread_spinlock_t lock __mpalign__;
        bool dead_;

        struct sufs_libfs_ch_item *head;
};

struct sufs_libfs_chainhash
{
        u64 nbuckets_;
        struct sufs_libfs_ch_bucket * buckets_;


        u64 nbuckets_resize_;
        struct sufs_libfs_ch_bucket * buckets_resize_;

        atomic_long size;
        atomic_int  seq_lock;
        bool dead_;
};

static inline bool sufs_libfs_chainhash_killed(
        struct sufs_libfs_chainhash *hash)
{
    return hash->dead_;
}

static inline void sufs_libfs_chainhash_fini(struct sufs_libfs_chainhash *hash)
{
    if (hash->buckets_)
        free(hash->buckets_);

    hash->dead_ = true;
    hash->buckets_ = NULL;
}

void sufs_libfs_chainhash_init(struct sufs_libfs_chainhash *hash, int index);

bool sufs_libfs_chainhash_lookup(struct sufs_libfs_chainhash *hash, char *key,
        int max_size, unsigned long *vptr1, unsigned long *vptr2);

bool sufs_libfs_chainhash_insert(struct sufs_libfs_chainhash *hash, char *key,
        int max_size, unsigned long val, unsigned long val2,
        struct sufs_libfs_ch_item ** item);

bool sufs_libfs_chainhash_remove(struct sufs_libfs_chainhash *hash, char *key,
        int max_size, unsigned long *val, unsigned long *val2);

bool sufs_libfs_chainhash_replace_from(
        struct sufs_libfs_chainhash *dst,
        char *kdst,
        unsigned long dst_exist,
        struct sufs_libfs_chainhash *src,
        char *ksrc,
        unsigned long vsrc,
        unsigned long vsrc2,
        int max_size,
        struct sufs_libfs_ch_item ** item);

bool sufs_libfs_chainhash_remove_and_kill(struct sufs_libfs_chainhash *hash);

void sufs_libfs_chainhash_forced_remove_and_kill(
        struct sufs_libfs_chainhash *hash);

bool sufs_libfs_chainhash_enumerate(struct sufs_libfs_chainhash *hash,
        int max_size, char *prev, char *out);

__ssize_t sufs_libfs_chainhash_getdents(struct sufs_libfs_chainhash *hash,
        int max_size, unsigned long * offset_ptr, void * buffer, size_t length);

#endif /* CHAINHASH_H_ */
