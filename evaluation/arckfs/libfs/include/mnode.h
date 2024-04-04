#ifndef SUFS_LIBFS_MNODE_H_
#define SUFS_LIBFS_MNODE_H_

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>

#include "../../include/libfs_config.h"
#include "../../include/common_inode.h"
#include "amd64.h"
#include "types.h"
#include "compiler.h"
#include "chainhash.h"
#include "radix_array.h"
#include "util.h"
#include "super.h"
#include "irwlock.h"
#include "range_lock.h"
#include "random.h"


extern struct sufs_libfs_mnode ** sufs_libfs_mnode_array;

struct sufs_libfs_radix_array;

/* TODO: cache line alignment */
struct sufs_libfs_dir_tail
{
    struct sufs_dir_entry * end_idx;
    pthread_spinlock_t lock;
};

struct sufs_libfs_mnode
{
    int ino_num;
    char type;

    int parent_mnum;

    struct sufs_inode * inode;

    struct sufs_fidx_entry * index_start;
    struct sufs_fidx_entry * index_end;

    /*
        atomic_long nlink_ __mpalign__;
        __padout__;
    */

    union
    {
        struct sufs_libfs_mnode_dir
        {
            int dir_node;
            pthread_spinlock_t index_lock;
            struct sufs_libfs_dir_tail * dir_tails;
            struct sufs_libfs_chainhash map_;

        } dir_data;

        struct sufs_libfs_mnode_file
        {
            int data_node;
#if SUFS_INODE_RW_LOCK == SUFS_INODE_RW_LOCK_SPIN
            pthread_spinlock_t rw_lock;
#elif SUFS_INODE_RW_LOCK == SUFS_INODE_RW_LOCK_BRAVO
            struct sufs_libfs_bravo_rwlock rw_lock;
#endif

#if SUFS_LIBFS_RANGE_LOCK
            struct sufs_libfs_irange_lock range_lock;
#endif
            struct sufs_libfs_radix_array pages_;
            unsigned long size_;
        } file_data;
    } data;
};

static inline void sufs_libfs_inode_init(struct sufs_inode * inode,
        char type, unsigned int mode, unsigned int uid, unsigned int gid,
        unsigned long offset)
{
    inode->file_type = type;
    inode->mode = mode;
    inode->uid = uid;
    inode->gid = gid;
    inode->size = 0;
    inode->offset = offset;

    /* Make them 0 for now */
    inode->atime = inode->ctime = inode->mtime = 0;
}

static inline char sufs_libfs_mnode_type(struct sufs_libfs_mnode * m)
{
   return m->type;
}

static inline bool sufs_libfs_mnode_dir_killed(struct sufs_libfs_mnode *mnode)
{
    return sufs_libfs_chainhash_killed(&mnode->data.dir_data.map_);
}


static inline bool sufs_libfs_mnode_dir_replace_from(
        struct sufs_libfs_mnode *dstparent,
        char *dstname,
        struct sufs_libfs_mnode *mdst,
        struct sufs_libfs_mnode *srcparent,
        char *srcname,
        struct sufs_libfs_mnode *msrc,
        struct sufs_libfs_mnode *subdir,
        struct sufs_libfs_ch_item ** item)
{
    return sufs_libfs_chainhash_replace_from(&dstparent->data.dir_data.map_,
            dstname, mdst ? 1 : 0, &srcparent->data.dir_data.map_, srcname,
                    msrc->ino_num, 0, SUFS_NAME_MAX, item);
}



static inline struct sufs_fidx_entry *
sufs_libfs_mnode_file_get_idx(struct sufs_libfs_mnode *m, u64 pageidx)
{
    struct sufs_fidx_entry * idx = (struct sufs_fidx_entry * )
            sufs_libfs_radix_array_find(&m->data.file_data.pages_, pageidx,
                    0, 0);

#if 0
    printf("find idx: %ld, v: %lx\n", pageidx, (unsigned long) idx);
#endif

    return idx;
}

/*
 * What we have in the radix tree is an pointer to the idx
 * This function returns the actual address of the page
 */
static inline unsigned long
sufs_libfs_mnode_file_get_page(struct sufs_libfs_mnode *m, u64 pageidx)
{
    struct sufs_fidx_entry * idx = sufs_libfs_mnode_file_get_idx(m, pageidx);

    if (idx == NULL)
        return 0;
    else
        return sufs_libfs_offset_to_virt_addr(idx->offset);
}

static inline void sufs_libfs_mnode_file_init(struct sufs_libfs_mnode *mnode)
{
    mnode->data.file_data.size_ = 0;

    mnode->data.file_data.data_node =
            sufs_libfs_xor_random() % (sufs_libfs_sb.pm_nodes);

#if SUFS_LIBFS_RANGE_LOCK
    mnode->data.file_data.range_lock.sg_table = NULL;
    mnode->data.file_data.range_lock.sg_size = 0;
#endif

    sufs_libfs_init_radix_array(&mnode->data.file_data.pages_,
            sizeof(unsigned long),
            ULONG_MAX / SUFS_PAGE_SIZE + 1,
            SUFS_PAGE_SIZE);
}

static inline void sufs_libfs_mnode_file_fill_index(struct sufs_libfs_mnode *m,
        u64 pageidx, unsigned long v)
{
#if 0
    printf("fill idx: %ld, v: %lx\n", pageidx, v);
#endif

    sufs_libfs_radix_array_find(&m->data.file_data.pages_, pageidx, 1, v);
}

static inline unsigned long sufs_libfs_mnode_file_size(struct sufs_libfs_mnode *m)
{
    return m->data.file_data.size_;
}

static inline void sufs_libfs_radix_array_fini(struct sufs_libfs_radix_array *ra);

static inline void sufs_libfs_mnode_file_free_page(struct sufs_libfs_mnode *m)
{
    sufs_libfs_radix_array_fini(&m->data.file_data.pages_);
}

static inline void sufs_libfs_mnode_dir_free(struct sufs_libfs_mnode *m)
{
    sufs_libfs_chainhash_fini(&m->data.dir_data.map_);
}

static inline void sufs_libfs_mnode_file_resize_nogrow(struct sufs_libfs_mnode *m,
        u64 newsize)
{
    u64 oldsize = m->data.file_data.size_;
    m->data.file_data.size_ = newsize;

    assert(FILE_BLOCK_ROUND_UP(newsize) <= FILE_BLOCK_ROUND_UP(oldsize));
}

static inline void sufs_libfs_mnode_free(struct sufs_libfs_mnode * mnode)
{
    if (mnode->type == SUFS_FILE_TYPE_REG)
    {
        sufs_libfs_inode_rwlock_destroy(mnode);
    #if SUFS_LIBFS_RANGE_LOCK
        sufs_libfs_irange_lock_free(&(mnode->data.file_data.range_lock));
    #endif
    }

    free(mnode);
}

void sufs_libfs_mnodes_init(void);

void sufs_libfs_mnodes_fini(void);

struct sufs_libfs_mnode*
sufs_libfs_mfs_mnode_init(u8 type, int ino_num, int parent_mnum,
        struct sufs_inode *inode);

void sufs_libfs_mnode_dir_init(struct sufs_libfs_mnode *mnode);

int sufs_libfs_mnode_dir_build_index(struct sufs_libfs_mnode *mnode);

struct sufs_libfs_mnode*
sufs_libfs_mnode_dir_lookup(struct sufs_libfs_mnode *mnode, char *name);

bool sufs_libfs_mnode_dir_enumerate(struct sufs_libfs_mnode *mnode, char *prev,
        char *name);

__ssize_t sufs_libfs_mnode_dir_getdents(struct sufs_libfs_mnode *mnode,
        unsigned long * offset_ptr, void * buffer, size_t length);

bool sufs_libfs_mnode_dir_entry_insert(struct sufs_libfs_mnode *mnode,
        char *name, int name_len, struct sufs_libfs_mnode *mf,
        struct sufs_dir_entry **dirp);

bool sufs_libfs_mnode_dir_insert(struct sufs_libfs_mnode *mnode, char *name,
        int name_len, struct sufs_libfs_mnode *mf, struct sufs_dir_entry **dirp);

bool sufs_libfs_mnode_dir_remove(struct sufs_libfs_mnode *mnode, char *name);

bool sufs_libfs_mnode_dir_kill(struct sufs_libfs_mnode *mnode);

struct sufs_libfs_mnode*
sufs_libfs_mnode_dir_exists(struct sufs_libfs_mnode *mnode, char *name);

void sufs_libfs_mnode_file_resize_nogrow(struct sufs_libfs_mnode *m, u64 newsize);

void sufs_libfs_mnode_file_resize_append(struct sufs_libfs_mnode *m, u64 newsize,
        unsigned long ps);

struct sufs_fidx_entry *
sufs_libfs_mnode_file_index_append(struct sufs_libfs_mnode *m,
        unsigned long addr);

void sufs_libfs_mnode_file_truncate_zero(struct sufs_libfs_mnode *m);

void sufs_libfs_mnode_file_delete(struct sufs_libfs_mnode *m);

int sufs_libfs_mnode_stat(struct sufs_libfs_mnode *m, struct stat *st);

#endif /* SUFS_MNODE_H_ */
