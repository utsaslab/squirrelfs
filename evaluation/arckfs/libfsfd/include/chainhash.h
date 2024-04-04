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
#include "hash.h"

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

        struct sufs_libfs_ch_item *head;
};

struct sufs_libfs_chainhash
{
        u64 nbuckets_;
        struct sufs_libfs_ch_bucket * buckets_;

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

static inline struct sufs_libfs_ch_bucket *
sufs_libfs_get_buckets(struct sufs_libfs_chainhash * hash, char * key, int max_size)
{
    struct sufs_libfs_ch_bucket * buckets = NULL;
    u64 nbuckets = 0;

    buckets = hash->buckets_;
    nbuckets = hash->nbuckets_;

    return (&(buckets[sufs_libfs_hash_string(key, max_size) % nbuckets]));
}

void sufs_libfs_chainhash_init(struct sufs_libfs_chainhash *hash, int index);

bool sufs_libfs_chainhash_lookup(struct sufs_libfs_chainhash *hash, char *key,
        int max_size, unsigned long *vptr1, unsigned long *vptr2);

bool sufs_libfs_chainhash_insert(struct sufs_libfs_chainhash *hash, char *key,
        int max_size, unsigned long val, unsigned long val2,
        struct sufs_libfs_ch_item ** item);

bool sufs_libfs_chainhash_remove(struct sufs_libfs_chainhash *hash, char *key,
        int max_size, unsigned long *val, unsigned long *val2);

void sufs_libfs_chainhash_forced_remove_and_kill(
        struct sufs_libfs_chainhash *hash);

#endif /* CHAINHASH_H_ */
