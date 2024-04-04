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
    bucket->dead_ = false;
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

    hash->nbuckets_resize_ = 0;
    hash->buckets_resize_ = NULL;

    hash->dead_ = false;
    hash->size = 0;

    sufs_libfs_seq_lock_init(&(hash->seq_lock));

    for (i = 0; i < hash->nbuckets_; i++)
    {
        sufs_libfs_chainhash_bucket_init(&(hash->buckets_[i]));
    }
}

struct sufs_libfs_ch_bucket *
sufs_libfs_chainhash_find_resize_buckets(struct sufs_libfs_chainhash *hash,
        char * key, int max_size)
{
    struct sufs_libfs_ch_bucket *buckets = NULL;
    u64 nbuckets = 0;
    struct sufs_libfs_ch_bucket *b = NULL;

    buckets = hash->buckets_resize_;
    nbuckets = hash->nbuckets_resize_;

    /* This can happen due to the completion of resize */
    while (buckets == NULL || nbuckets == 0)
    {
        buckets = hash->buckets_;
        nbuckets = hash->nbuckets_;
    }

    b = &(buckets[sufs_libfs_hash_string(key, max_size) % nbuckets]);

    return b;
}

static struct sufs_libfs_ch_bucket *
sufs_libfs_get_buckets(struct sufs_libfs_chainhash * hash, char * key, int max_size)
{
    int bseq = 0, eseq = 0;
    struct sufs_libfs_ch_bucket * buckets = NULL;
    u64 nbuckets = 0;

    do
    {
        bseq = sufs_libfs_seq_lock_read(&(hash->seq_lock));

        buckets = hash->buckets_;
        nbuckets = hash->nbuckets_;

        eseq = sufs_libfs_seq_lock_read(&(hash->seq_lock));

    } while (sufs_libfs_seq_lock_retry(bseq, eseq));

    return (&(buckets[sufs_libfs_hash_string(key, max_size) % nbuckets]));
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

    if (b->dead_)
    {
#if 0
        pthread_spin_unlock(&b->lock);
#endif

        b = sufs_libfs_chainhash_find_resize_buckets(hash, key, max_size);

#if 0
        pthread_spin_lock(&b->lock);
#endif
    }

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

static unsigned long
sufs_libfs_chainhash_new_size(struct sufs_libfs_chainhash * hash, int enlarge)
{
    int i = 0;
    for (i = 0; i < SUFS_LIBFS_HASH_SIZES_MAX; i++)
    {
        if (sufs_libfs_hash_sizes[i] == hash->nbuckets_)
            break;
    }

    if (enlarge)
    {
        if (i == SUFS_LIBFS_HASH_SIZES_MAX)
        {
            fprintf(stderr, "Hash reaches maximum size!\n");
            return 0;
        }

        i++;
    }
    else
    {
        if (i == 0)
        {
            fprintf(stderr, "Bug: reducing the size of a minimum hash!\n");
            return 0;
        }

        i--;
    }

    return sufs_libfs_hash_sizes[i];
}

static void sufs_libfs_chainhash_resize(struct sufs_libfs_chainhash * hash,
        int enlarge, int max_size)
{
    int i = 0;

#if 0
    return;
#endif

    /* The resize is already in progress, return */
    if (!__sync_bool_compare_and_swap(&hash->nbuckets_resize_, 0, 1))
        return;

    hash->nbuckets_resize_= sufs_libfs_chainhash_new_size(hash, enlarge);

    if (!hash->nbuckets_resize_)
    {
        return;
    }

    /* init the resize hash */
    hash->buckets_resize_ = malloc(hash->nbuckets_resize_ *
            sizeof(struct sufs_libfs_ch_bucket));

    if (!hash->buckets_resize_)
    {
        fprintf(stderr, "Cannot malloc the new hash table");
        abort();
    }

    for (i = 0; i < hash->nbuckets_resize_; i++)
    {
        sufs_libfs_chainhash_bucket_init(&(hash->buckets_resize_[i]));
    }


    /* move the entry in the old hash to the new one */
    for (i = 0; i < hash->nbuckets_; i++)
    {
        struct sufs_libfs_ch_item *iter = NULL;
        struct sufs_libfs_ch_bucket *b = NULL;

        b = &(hash->buckets_[i]);

        pthread_spin_lock(&(b->lock));

        b->dead_ = true;

        iter = hash->buckets_[i].head;
        while (iter)
        {
            struct sufs_libfs_ch_item *prev = NULL;
            struct sufs_libfs_ch_bucket *nb = NULL;

            prev = iter;
            iter = iter->next;

            nb =  &hash->buckets_resize_[sufs_libfs_hash_string(prev->key, max_size)
                                         % hash->nbuckets_resize_];
#if 0
            printf("resize item: %s\n", prev->key);
            if (strcmp(prev->key, "1216ugwzkisybd") == 0)
            {
                printf("bucket is %ld, size: %ld, max_size: %d\n", sufs_libfs_hash_string(prev->key, max_size)
                                         % hash->nbuckets_resize_, hash->nbuckets_resize_, max_size);
            }
#endif

            pthread_spin_lock(&(nb->lock));

            prev->next = nb->head;
            nb->head = prev;

            pthread_spin_unlock(&(nb->lock));
        }

        hash->buckets_[i].head = NULL;

        pthread_spin_unlock(&(b->lock));
    }

    /* swap back */
    sufs_libfs_seq_lock_write_begin(&hash->seq_lock);

    hash->buckets_ = hash->buckets_resize_;
    hash->nbuckets_ = hash->nbuckets_resize_;

    sufs_libfs_seq_lock_write_end(&hash->seq_lock);

    hash->buckets_resize_ = NULL;

    /*
     * This seems like both a compiler and a hardware fence
     * https://stackoverflow.com/questions/982129/what-does-sync-synchronize-do
     */

    /* May not need this given x86 is TSO */
    /* __sync_synchronize(); */

    hash->nbuckets_resize_ = 0;

    return;
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

    if (b->dead_)
    {
        pthread_spin_unlock(&b->lock);

        b = sufs_libfs_chainhash_find_resize_buckets(hash, key, max_size);

        pthread_spin_lock(&b->lock);
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

#if 0
    printf("insert: key=%s, val=%ld, val2=%ld\n", key, val, val2);
#endif

    sufs_libfs_ch_item_init(i, key, max_size, val, val2);


    i->next = b->head;
    b->head = i;

    ret = true;

    if (item)
        *item = i;

    hash->size++;

out:
    pthread_spin_unlock(&b->lock);

    /* TODO: Make the test a function.. */
    if (ret && hash->nbuckets_ != sufs_libfs_hash_max_size &&
            hash->size > hash->nbuckets_ * SUFS_LIBFS_DIR_REHASH_FACTOR)
    {
        sufs_libfs_chainhash_resize(hash, 1, max_size);
    }


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

    if (b->dead_)
    {
        pthread_spin_unlock(&b->lock);

        b = sufs_libfs_chainhash_find_resize_buckets(hash, key, max_size);

        pthread_spin_lock(&b->lock);
    }


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
            hash->size--;
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

    /* TODO: Make the test a function.. */
    if (ret && hash->nbuckets_ != sufs_libfs_hash_min_size &&
            hash->size * SUFS_LIBFS_DIR_REHASH_FACTOR < hash->nbuckets_)
    {
        sufs_libfs_chainhash_resize(hash, 0, max_size);
    }


    return ret;
}

bool sufs_libfs_chainhash_replace_from(
        struct sufs_libfs_chainhash *dst,
        char *kdst,
        unsigned long dst_exist,
        struct sufs_libfs_chainhash *src,
        char *ksrc,
        unsigned long vsrc,
        unsigned long vsrc2,
        int max_size,
        struct sufs_libfs_ch_item ** item)
{
    /*
     * A special API used by rename.  Atomically performs the following
     * steps, returning false if any of the checks fail:
     *
     *  For file renames:
     *  - checks that this hash table has not been killed (by unlink)
     *  - if vpdst!=nullptr, checks this[kdst]==*vpdst
     *  - if vpdst==nullptr, checks this[kdst] is not set
     *  - checks src[ksrc]==vsrc
     *  - removes src[ksrc]
     *  - sets this[kdst] = vsrc
     *
     */

    int index = 0, i = 0, j = 0;
    struct sufs_libfs_ch_bucket *bdst = NULL;

    struct sufs_libfs_ch_bucket *bsrc = NULL;

    struct sufs_libfs_ch_item *srci = NULL, *srcprev = NULL, *dsti = NULL,
            *item_tmp = NULL;

    bool ret = 0, free_src = 0;

    /*
     * Acquire the locks for the source and destination directory
     * hash tables in the order of increasing bucket addresses.
     */

    struct sufs_libfs_ch_bucket *buckets[2];


    bdst = sufs_libfs_get_buckets(dst, kdst, max_size);
    bsrc = sufs_libfs_get_buckets(src, ksrc, max_size);

lock:
    index = 0;

    if (bsrc != bdst)
    {
        buckets[index] = bsrc;
        index++;
    }

    buckets[index] = bdst;
    index++;

    for (i = 0; i < index - 1; i++)
    {
        for (j = i + 1; j < index; j++)
        {
            if ((unsigned long) buckets[j] < (unsigned long) buckets[i])
            {
                struct sufs_libfs_ch_bucket *tmp;
                tmp = buckets[j], buckets[j] = buckets[i], buckets[i] = tmp;
            }
        }
    }

    for (i = 0; i < index; i++)
        pthread_spin_lock(&buckets[i]->lock);

    if (bsrc->dead_)
    {
        for (i = 0; i < index; i++)
            pthread_spin_unlock(&buckets[i]->lock);

        bsrc = sufs_libfs_chainhash_find_resize_buckets(src, ksrc, max_size);
        goto lock;
    }

    if (bdst->dead_)
    {
        for (i = 0; i < index; i++)
            pthread_spin_unlock(&buckets[i]->lock);

        bdst = sufs_libfs_chainhash_find_resize_buckets(dst, kdst, max_size);
        goto lock;
    }

    /*
     * Abort the rename if the destination directory's hash table has been
     * killed by a concurrent unlink.
     */
    if (dst->dead_)
    {
        ret = false;
        goto out;
    }


    /* Find the source */
    srci = bsrc->head;
    srcprev = NULL;
    while (1)
    {
        if (srci == NULL)
        {
            ret = false;
            goto out;
        }

        if (strcmp(srci->key, ksrc) == 0)
        {
            break;
        }

        srcprev = srci;
        srci = srci->next;
    }

    /* Find the destination */
    dsti = bdst->head;

    while (dsti != NULL)
    {
        if (strcmp(dsti->key, kdst) == 0)
        {
            if (!dst_exist)
            {
                ret = false;
                goto out;
            }

            dsti->val = vsrc;
            dsti->val2 = vsrc2;

            if (item)
                *item = dsti;

            if (srcprev == NULL)
            {
                bsrc->head = srci->next;
            }
            else
            {
                srcprev->next = srci->next;
            }

            free_src = 1;
            ret = true;
            goto out;
        }

        dsti = dsti->next;
    }

    if (dst_exist)
    {
        ret = false;
        goto out;
    }

    if (srcprev == NULL)
    {
        bsrc->head = srci->next;
    }
    else
    {
        srcprev->next = srci->next;
    }

    free_src = 1;

    item_tmp = malloc(sizeof(struct sufs_libfs_ch_item));

    sufs_libfs_ch_item_init(item_tmp, kdst, max_size, vsrc, vsrc2);

    item_tmp->next = bdst->head;
    bdst->head = item_tmp;

    ret = true;

    if (item)
        *item = item_tmp;

out:
    for (i = 0; i < index; i++)
        pthread_spin_unlock(&buckets[i]->lock);

    if (free_src)
        sufs_libfs_ch_item_free(srci);


    src->size--;
    dst->size++;

    /* TODO: Make the test a function.. */
    if (ret && src->nbuckets_!= sufs_libfs_hash_min_size &&
            src->size * SUFS_LIBFS_DIR_REHASH_FACTOR < src->nbuckets_)
    {
        sufs_libfs_chainhash_resize(src, 0, max_size);
    }

    if (ret && dst->nbuckets_ != sufs_libfs_hash_max_size &&
            dst->size > dst->nbuckets_ * SUFS_LIBFS_DIR_REHASH_FACTOR)
    {
        sufs_libfs_chainhash_resize(dst, 1, max_size);
    }

    return ret;
}

bool sufs_libfs_chainhash_remove_and_kill(struct sufs_libfs_chainhash *hash)
{
    if (hash->dead_ || hash->size != 0)
        return false;

    hash->dead_ = true;

    return true;
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

/*
 * This needs to be rewritten to fit the getdentries,
 * leave it as it is for now
 */
bool sufs_libfs_chainhash_enumerate(struct sufs_libfs_chainhash *hash,
        int max_size, char *prev, char *out)
{
    u64 i = 0;
    bool prevbucket = (prev != NULL);

#if 0
    printf("hash size upon enumerate :%ld\n", hash->size);
#endif

    i = prev ? sufs_libfs_hash_string(prev, max_size) % hash->nbuckets_ :
             0;

#if 0
    printf("prev: %s, i is %ld, size: %ld\n", prev, i, hash->nbuckets_);
#endif

    for (; i < hash->nbuckets_; i++)
    {
        struct sufs_libfs_ch_bucket *b = &hash->buckets_[i];
        struct sufs_libfs_ch_item *item;
        bool found = false;

#if 0
        pthread_spin_lock(&hash->buckets_[i].lock);
#endif

        for (item = b->head; item != NULL; item = item->next)
        {
            if (prevbucket)
            {
                if (strcmp(item->key, prev) == 0)
                    prevbucket = false;
            }
            else
            {
                strcpy(out, item->key);

#if 0
                printf("out is %s, i is %ld\n", out, i);
#endif

                found = true;
                break;
            }
        }

#if 0
        pthread_spin_unlock(&hash->buckets_[i].lock);
#endif

        if (found)
            return true;

        prevbucket = false;
    }

    return false;
}


__ssize_t sufs_libfs_chainhash_getdents(struct sufs_libfs_chainhash *hash,
        int max_size, unsigned long * offset_ptr, void * buffer, size_t length)
{
    unsigned long offset = (*offset_ptr);

    struct sufs_dir_entry * dir = NULL;
    unsigned long count = 0;

    /*
     * MS 32 bits: which bucket,
     * LS 32 bits: offset with in the bucket
     */

    u64 i = offset >> 32;
    u64 j = offset & 0xffffffff;

#if 0
    printf("enter: i: %ld, j: %ld, length: %ld, tot_dentry: %ld\n", i, j, length,
            hash->size);
#endif

    struct linux_dirent64 * iter_ptr = buffer;
    size_t iter_offset = 0;

    for (; i < hash->nbuckets_; i++)
    {
        count = 0;
        struct sufs_libfs_ch_bucket *b = &hash->buckets_[i];
        struct sufs_libfs_ch_item *item = NULL;

#if 0
        pthread_spin_lock(&hash->buckets_[i].lock);
#endif

        for (item = b->head; item != NULL; item = item->next)
        {
            if (j > 0)
            {
                j--;
            }
            else
            {
                long dent_size = 0;
                dir = (struct sufs_dir_entry *) item->val2;

                dent_size = sizeof(struct linux_dirent64) + dir->name_len - 1;

                if (iter_offset + dent_size > length)
                {
#if 0
                    pthread_spin_unlock(&hash->buckets_[i].lock);
#endif
                    goto out;
                }
                iter_ptr->d_ino = item->val;
                iter_ptr->d_off = iter_ptr->d_reclen = dent_size;

                if (dir->inode.file_type == SUFS_FILE_TYPE_REG)
                    iter_ptr->d_type = DT_REG;
                else
                    iter_ptr->d_type = DT_DIR;

                strcpy(iter_ptr->d_name, dir->name);

                iter_offset += dent_size;
                iter_ptr = (struct linux_dirent64 *)
                        ((unsigned long) buffer + iter_offset);
            }

            count++;
        }

#if 0
        pthread_spin_unlock(&hash->buckets_[i].lock);
#endif

        j = 0;
    }

out:
    (*offset_ptr) = ((i << 32) | count);

#if 0
    printf("leave: i: %ld, j: %ld, return: %ld\n", i, count, iter_offset);
#endif

    return iter_offset;
}
