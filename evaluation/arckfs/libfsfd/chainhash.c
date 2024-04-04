#include <stdbool.h>
#include <stdio.h>

#include "../include/libfs_config.h"
#include "chainhash.h"
#include "hash.h"
#include "ialloc.h"
#include "syscall.h"
#include "seqlock.h"

#define SUFS_LIBFS_HASH_SIZES_MAX  15

static const long sufs_libfs_hash_sizes[SUFS_LIBFS_HASH_SIZES_MAX] =
{
    1063,
    2153,
    4363,
    8219,
    16763,
    32957,
    64601,
    128983,
    256541,
    512959,
    1024921,
    2048933,
    4096399,
    8192003,
    16384001
};

const long sufs_libfs_hash_min_size =
        sufs_libfs_hash_sizes[0];

const long sufs_libfs_hash_max_size =
        sufs_libfs_hash_sizes[SUFS_LIBFS_HASH_SIZES_MAX - 1];

/*
 * value : ino
 * value2: ptr to struct sufs_dir_entry
 */
static inline void
sufs_libfs_ch_item_init(struct sufs_libfs_ch_item *item, char *key,
        unsigned long size, unsigned value, unsigned long value2)
{
    size = strlen(key) + 1;
    item->key = malloc(size);
    strcpy(item->key, key);

#if 0
    printf("key is %s, item key is %s\n", key, item->key);
#endif

    item->val = value;
    item->val2 = value2;
}

static inline void sufs_libfs_ch_item_free(struct sufs_libfs_ch_item *item)
{
    if (item->key)
        free(item->key);

    if (item)
        free(item);
}

static inline void
sufs_libfs_chainhash_bucket_init(struct sufs_libfs_ch_bucket * bucket)
{
    pthread_spin_init(&(bucket->lock), PTHREAD_PROCESS_SHARED);

    bucket->head = NULL;
}

void sufs_libfs_chainhash_init(struct sufs_libfs_chainhash *hash, int index)
{
    int i = 0;

    if (index < 0 || index >= SUFS_LIBFS_HASH_SIZES_MAX)
    {
        fprintf(stderr, "index :%d for hash table is too large!\n", index);
        abort();
    }

    u64 nbuckets = sufs_libfs_hash_sizes[index];

    hash->nbuckets_ = nbuckets;
    hash->buckets_ = malloc(hash->nbuckets_ *
            sizeof(struct sufs_libfs_ch_bucket));

    hash->dead_ = false;

    for (i = 0; i < hash->nbuckets_; i++)
    {
        sufs_libfs_chainhash_bucket_init(&(hash->buckets_[i]));
    }
}

bool sufs_libfs_chainhash_lookup(struct sufs_libfs_chainhash *hash, char *key,
        int max_size, unsigned long *vptr, unsigned long *vptr2)
{
    struct sufs_libfs_ch_item *i = NULL;
    struct sufs_libfs_ch_bucket *b = NULL;
    bool ret;

    b = sufs_libfs_get_buckets(hash, key, max_size);

#if 0
    pthread_spin_lock(&b->lock);
#endif


    for (i = b->head; i != NULL; i = i->next)
    {
        if (strcmp(i->key, key) != 0)
            continue;

        if (vptr)
            (*vptr) = i->val;

        if (vptr2)
            (*vptr2) = i->val2;

        ret = true;
        goto out;
    }

    ret = false;

out:
#if 0
    pthread_spin_unlock(&b->lock);
#endif
    return ret;
}



bool sufs_libfs_chainhash_insert(struct sufs_libfs_chainhash *hash, char *key,
        int max_size, unsigned long val, unsigned long val2,
        struct sufs_libfs_ch_item ** item)
{
    bool ret = false;
    struct sufs_libfs_ch_item *i = NULL;
    struct sufs_libfs_ch_bucket *b = NULL;

    if (hash->dead_)
    {
#if 0
        printf("hash is dead_!\n");
        fflush(stdout);
#endif
        return false;
    }

    b = sufs_libfs_get_buckets(hash, key, max_size);

    pthread_spin_lock(&b->lock);

    if (hash->dead_)
    {
#if 0
        printf("hash is dead_!\n");
        fflush(stdout);
#endif

        ret = false;
        goto out;
    }

    for (i = b->head; i != NULL; i = i->next)
    {
        if (strcmp(i->key, key) == 0)
        {
#if 0
            printf("i->key is %s, key is %s!\n", i->key, key);
            fflush(stdout);
#endif

            ret = false;
            goto out;
        }
    }

    i = malloc(sizeof(struct sufs_libfs_ch_item));

#if 0
    if (strcmp(key, "001juxaacivvy") == 0)
    {
        printf("index is %ld\n", sufs_libfs_hash_string(key, max_size) % hash->nbuckets_);
    }
#endif

    sufs_libfs_ch_item_init(i, key, max_size, val, val2);

    i->next = b->head;
    b->head = i;

    ret = true;

    if (item)
        *item = i;

out:
    pthread_spin_unlock(&b->lock);

#if 0
    printf("hash size upon insert :%ld\n", hash->size);
#endif

    return ret;
}

bool sufs_libfs_chainhash_remove(struct sufs_libfs_chainhash *hash, char *key,
        int max_size, unsigned long *val, unsigned long *val2)
{
    bool ret = false;

    struct sufs_libfs_ch_bucket *b = NULL;
    struct sufs_libfs_ch_item *i = NULL, *prev = i;

    b = sufs_libfs_get_buckets(hash, key, max_size);

    pthread_spin_lock(&b->lock);

    for (i = b->head; i != NULL; i = i->next)
    {
        if (strcmp(i->key, key) == 0)
        {
            if (prev == NULL)
            {
                b->head = i->next;
            }
            else
            {
                prev->next = i->next;
            }

            ret = true;
            goto out;
        }

        prev = i;
    }

out:
    pthread_spin_unlock(&b->lock);

    if (ret)
    {
        if (val)
        {
            (*val) = i->val;
        }

        if (val2)
        {
            (*val2) = i->val2;
        }

        sufs_libfs_ch_item_free(i);
    }


    return ret;
}


/* This will not be executed concurrently with resize so we are good */
void sufs_libfs_chainhash_forced_remove_and_kill(
        struct sufs_libfs_chainhash *hash)
{
    int i = 0;

    for (i = 0; i < hash->nbuckets_; i++)
        pthread_spin_lock(&hash->buckets_[i].lock);

    for (i = 0; i < hash->nbuckets_; i++)
    {
        struct sufs_libfs_ch_item *iter = hash->buckets_[i].head;
        while (iter)
        {
            struct sufs_libfs_ch_item *prev = iter;

            /* This breaks all the abstractions. Oh, well... */
            sufs_libfs_inode_clear_allocated(iter->val);

            iter = iter->next;
            sufs_libfs_ch_item_free(prev);
        }
    }

    hash->dead_ = true;

    for (i = 0; i < hash->nbuckets_; i++)
        pthread_spin_unlock(&hash->buckets_[i].lock);
}
