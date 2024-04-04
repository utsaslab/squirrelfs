#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>

#include "../include/libfs_config.h"
#include "../include/common_inode.h"
#include "mnode.h"
#include "chainhash.h"
#include "mfs.h"
#include "amd64.h"
#include "atomic_util.h"
#include "util.h"
#include "journal.h"
#include "random.h"

struct sufs_libfs_mnode **sufs_libfs_mnode_array = NULL;

void sufs_libfs_mnodes_init(void)
{
    sufs_libfs_mnode_array = (struct sufs_libfs_mnode**) calloc(
            SUFS_MAX_INODE_NUM, sizeof(struct sufs_libfs_mnode));

    if (sufs_libfs_mnode_array == NULL)
    {
        fprintf(stderr, "allocate mnode array failed!\n");
        abort();
    }
}

void sufs_libfs_mnodes_fini(void)
{
    if (sufs_libfs_mnode_array)
        free(sufs_libfs_mnode_array);
}

static struct sufs_libfs_mnode* sufs_libfs_mfs_do_mnode_init(u8 type,
        int ino_num, int parent_mnum, struct sufs_inode *inode)
{
    struct sufs_libfs_mnode *m = NULL;

    m = malloc(sizeof(struct sufs_libfs_mnode));

    if (!m)
    {
        fprintf(stderr, "allocate mnode failed in %s", __func__);
        return NULL;
    }

    m->ino_num = ino_num;
    m->type = type;

    m->parent_mnum = parent_mnum;
    m->inode = inode;
    m->index_start = NULL;
    m->index_end = NULL;

    sufs_libfs_inode_rwlock_init(m);

    switch (type)
    {
        case SUFS_FILE_TYPE_REG:
            sufs_libfs_mnode_file_init(m);
            break;

        case SUFS_FILE_TYPE_DIR:
            sufs_libfs_mnode_dir_init(m);
            break;

        default:
            fprintf(stderr, "unknown type in mnum %d\n", ino_num);
            abort();
    }

    return m;
}

static void sufs_libfs_mnode_file_unmap(struct sufs_libfs_mnode *m);

struct sufs_libfs_mnode*
sufs_libfs_mfs_mnode_init(u8 type, int ino_num, int parent_mnum,
        struct sufs_inode *inode)
{
#if 0
    printf("ino_num is %ld\n", ino_num);
    fflush(stdout);
#endif
    struct sufs_libfs_mnode *mnode = NULL;

    /* This could happen when another trust group obtains the file */
    if (sufs_libfs_mnode_array[ino_num])
    {
        /* 
         * BUG: sometime this code is executed even without trust group, 
         * comment it for now 
         */

        /* 
         * sufs_libfs_mnode_file_unmap(sufs_libfs_mnode_array[ino_num]);
         * sufs_libfs_mnode_free(sufs_libfs_mnode_array[ino_num]);
         */
    }

    mnode = sufs_libfs_mfs_do_mnode_init(type, ino_num, parent_mnum,
            inode);

    sufs_libfs_mnode_array[ino_num] = mnode;

    return mnode;
}

void sufs_libfs_mnode_dir_init(struct sufs_libfs_mnode *mnode)
{
    int i = 0;

    /*
     * This is bad, make it as it is right now since the code has not been
     * finalized
     */
    int cpus = sufs_libfs_sb.cpus_per_socket * sufs_libfs_sb.sockets;

    mnode->data.dir_data.dir_node = sufs_libfs_current_node();

    mnode->data.dir_data.dir_tails =
            malloc(sizeof(struct sufs_libfs_dir_tail) * cpus);

    for (i = 0; i < cpus; i++)
    {
        pthread_spin_init(&(mnode->data.dir_data.dir_tails[i].lock),
                PTHREAD_PROCESS_SHARED);
        mnode->data.dir_data.dir_tails[i].end_idx = NULL;
    }

    pthread_spin_init(&(mnode->data.dir_data.index_lock), PTHREAD_PROCESS_SHARED);

    sufs_libfs_chainhash_init(&mnode->data.dir_data.map_,
            SUFS_LIBFS_DIR_INIT_HASH_IDX);
}

static int
sufs_libfs_mnode_dir_build_index_one_file_block(struct sufs_libfs_mnode * mnode,
        unsigned long offset)
{
    struct sufs_dir_entry *dir = (struct sufs_dir_entry *)
            sufs_libfs_offset_to_virt_addr(offset);

    int cpu = 0;

    while (dir->name_len != 0)
    {
#if 0
        printf("dir is %lx, ino_num is %d, len: %d\n", (unsigned long) dir,
                dir->ino_num, dir->rec_len);
#endif

        if (dir->ino_num != SUFS_INODE_TOMBSTONE)
        {
            if (sufs_libfs_mfs_mnode_init(dir->inode.file_type, dir->ino_num,
                    mnode->ino_num, &dir->inode) == NULL)
                return -ENOMEM;

            if (sufs_libfs_chainhash_insert(&mnode->data.dir_data.map_,
                    dir->name, SUFS_NAME_MAX, dir->ino_num,
                    (unsigned long) dir, NULL) == false)
                return -ENOMEM;
        }

        dir = (struct sufs_dir_entry *)
                ((unsigned long) dir + dir->rec_len);

        if (FILE_BLOCK_OFFSET(dir) == 0)
            break;
    }

    cpu = sufs_libfs_current_cpu();

    mnode->data.dir_data.dir_tails[cpu].end_idx = dir;
    return 0;
}

int sufs_libfs_mnode_dir_build_index(struct sufs_libfs_mnode *mnode)
{
    struct sufs_fidx_entry *idx = mnode->index_start;

#if 0
      printf("index_start is: %lx\n", idx);
#endif

    if (idx == NULL)
        goto out;

    while (idx->offset != 0)
    {
        if (likely(sufs_is_norm_fidex(idx)))
        {
#if 0
            printf("building index:%lx for offset: %lx\n", idx, idx->offset);
#endif
            sufs_libfs_mnode_dir_build_index_one_file_block(mnode, idx->offset);
            idx++;
        }
        else
        {

            idx = (struct sufs_fidx_entry*) sufs_libfs_offset_to_virt_addr(
                    idx->offset);
        }
    }

out:

#if 0
    printf("self: %lx, index_end is: %lx\n", pthread_self(), idx);
    printf("self: %lx, offset of index_end is %lx\n", pthread_self(), idx->offset);
#endif
    mnode->index_end = idx;

    return 0;
}

struct sufs_libfs_mnode*
sufs_libfs_mnode_dir_lookup(struct sufs_libfs_mnode *mnode, char *name)
{
    unsigned long ino = 0;

    if (strcmp(name, ".") == 0)
        return mnode;

    if (strcmp(name, "..") == 0)
        return sufs_libfs_mnode_array[mnode->parent_mnum];

    sufs_libfs_file_enter_cs(mnode);

#if 0
    printf("mnode %d map: %d\n", mnode->ino_num, sufs_libfs_file_is_mapped(mnode));
#endif

    if (sufs_libfs_map_file(mnode, 0) != 0)
    {
#if 0
        printf("Failed at sufs_libfs_map_file!\n");
#endif
        goto out;
    }

    sufs_libfs_chainhash_lookup(&mnode->data.dir_data.map_, name, SUFS_NAME_MAX,
            &ino, NULL);

#if 0
        printf("name is %s, ino is %ld!\n", name, ino);
#endif

out:
    sufs_libfs_file_exit_cs(mnode);
    return sufs_libfs_mnode_array[ino];
}

bool sufs_libfs_mnode_dir_enumerate(struct sufs_libfs_mnode *mnode, char *prev,
        char *name)
{
    bool ret = false;
    if (strcmp(prev, "") == 0)
    {
        strcpy(name, ".");
        return true;
    }

    if (strcmp(prev, ".") == 0)
    {
        strcpy(name, "..");
        return true;
    }

    if (strcmp(prev, "..") == 0)
        prev = NULL;

    sufs_libfs_file_enter_cs(mnode);

    if (sufs_libfs_map_file(mnode, 0) != 0)
        goto out;

    ret = sufs_libfs_chainhash_enumerate(&mnode->data.dir_data.map_,
            SUFS_NAME_MAX, prev, name);

out:
    sufs_libfs_file_exit_cs(mnode);
    return ret;
}

__ssize_t sufs_libfs_mnode_dir_getdents(struct sufs_libfs_mnode *mnode,
        unsigned long * offset_ptr, void * buffer, size_t length)
{
    __ssize_t ret = 0;

    sufs_libfs_file_enter_cs(mnode);

    if (sufs_libfs_map_file(mnode, 0) != 0)
        goto out;

    ret = sufs_libfs_chainhash_getdents(&mnode->data.dir_data.map_,
            SUFS_NAME_MAX, offset_ptr, buffer, length);

out:
    sufs_libfs_file_exit_cs(mnode);
    return ret;
}

bool sufs_libfs_mnode_dir_entry_insert(struct sufs_libfs_mnode *mnode,
        char *name, int name_len, struct sufs_libfs_mnode *mf,
        struct sufs_dir_entry **dirp)
{
    struct sufs_dir_entry *dir = NULL;

    bool ret = true;

    if (name_len > SUFS_NAME_MAX)
        return false;

    int record_len = sizeof(struct sufs_dir_entry) + name_len;

    int cpu = sufs_libfs_current_cpu();

    pthread_spin_lock(&(mnode->data.dir_data.dir_tails[cpu].lock));

    dir = mnode->data.dir_data.dir_tails[cpu].end_idx;

#if 0
    printf("insert: dir address is %lx\n", (unsigned long) dir);
#endif

    if ((dir == NULL) ||
            (FILE_BLOCK_OFFSET(dir) + record_len > SUFS_FILE_BLOCK_SIZE))
    {
        unsigned long block_nr = 0;

        struct sufs_fidx_entry * idx = NULL;

        sufs_libfs_new_dir_data_blocks(&sufs_libfs_sb, mnode, &block_nr,
                SUFS_FILE_BLOCK_PAGE_CNT, 1);

        if (!block_nr)
        {
            pthread_spin_unlock(&(mnode->data.dir_data.dir_tails[cpu].lock));
            return false;
        }

        dir = (struct sufs_dir_entry*) sufs_libfs_block_to_virt_addr(
                (unsigned long) block_nr);

        pthread_spin_lock(&(mnode->data.dir_data.index_lock));
        idx = sufs_libfs_mnode_file_index_append(mnode, (unsigned long) dir);
        pthread_spin_unlock(&(mnode->data.dir_data.index_lock));

        if (idx)
            sufs_libfs_flush_file_index(idx, idx);
    }

    mnode->data.dir_data.dir_tails[cpu].end_idx =
            (struct sufs_dir_entry *) ((unsigned long) dir + record_len);

    /*
     * reset the end_idx to NULL if it spans across pages, so that the next
     * insert will create new block numbers
     */

    if (FILE_BLOCK_ROUND_DOWN(mnode->data.dir_data.dir_tails[cpu].end_idx)
            != FILE_BLOCK_ROUND_DOWN(dir))
    {
        mnode->data.dir_data.dir_tails[cpu].end_idx = NULL;
    }

    pthread_spin_unlock(&(mnode->data.dir_data.dir_tails[cpu].lock));

#if 0
    printf("dir address: %lx, offset: %ld\n",
            (unsigned long) dir, FILE_BLOCK_OFFSET(dir));
#endif

    strcpy(dir->name, name);
    dir->ino_num = mf->ino_num;
    dir->rec_len = record_len;

#if 0
    printf("dir->name address: %lx, offset: %ld, len: %ld\n",
            (unsigned long) dir->name, FILE_BLOCK_OFFSET(dir->name),
            strlen(name));
#endif

    if (dirp) {
        (*dirp) = dir;
    }

    ret = true;

    return ret;

}

bool sufs_libfs_mnode_dir_insert(struct sufs_libfs_mnode *mnode, char *name,
        int name_len, struct sufs_libfs_mnode *mf, struct sufs_dir_entry **dirp)
{
    bool ret = false;
    struct sufs_dir_entry *dir = NULL;
    struct sufs_libfs_ch_item * item = NULL;

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return false;

    sufs_libfs_file_enter_cs(mnode);

    if (sufs_libfs_map_file(mnode, 1) != 0)
    {
#if 0
        printf("map failed!\n");
        fflush(stdout);
#endif
        goto out;
    }

    if (!sufs_libfs_chainhash_insert(&mnode->data.dir_data.map_, name,
            SUFS_NAME_MAX, mf->ino_num, 0, &item))
    {
#if 0
        printf("hash insert failed!\n");
        fflush(stdout);
#endif
        goto out;
    }

    if (!sufs_libfs_mnode_dir_entry_insert(mnode, name, name_len, mf, &dir))
    {
#if 0
        printf("dir insert failed!\n");
        fflush(stdout);
#endif
        goto out;
    }

    item->val2 = (unsigned long) dir;
    ret = true;

#if 0
    printf("insert complete: name:%s, item:%lx, val:%ld, val2:%ld\n", name, (unsigned long) item, item->val, item->val2);
#endif

    if (dirp)
    {
        (*dirp) = dir;
    }

out:
    sufs_libfs_file_exit_cs(mnode);
    return ret;
}

bool sufs_libfs_mnode_dir_remove(struct sufs_libfs_mnode *mnode, char *name)
{
    bool ret = false;
    struct sufs_dir_entry *dir;

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return false;

    sufs_libfs_file_enter_cs(mnode);

    if (sufs_libfs_map_file(mnode, 1) != 0)
        goto out;

    if (!sufs_libfs_chainhash_remove(&mnode->data.dir_data.map_, name,
            SUFS_NAME_MAX, NULL, (unsigned long*) &dir))
    {
        goto out;
    }

    dir->ino_num = SUFS_INODE_TOMBSTONE;

    sufs_libfs_clwb_buffer(&(dir->ino_num), sizeof(dir->ino_num));
    sufs_libfs_sfence();

    ret = true;

out:
    sufs_libfs_file_exit_cs(mnode);
    return ret;
}

bool sufs_libfs_mnode_dir_kill(struct sufs_libfs_mnode *mnode)
{
    bool ret = false;
    sufs_libfs_file_enter_cs(mnode);

    if (sufs_libfs_map_file(mnode, 0) != 0)
        goto out;

    ret = sufs_libfs_chainhash_remove_and_kill(&mnode->data.dir_data.map_);
out:
    sufs_libfs_file_exit_cs(mnode);
    return ret;
}

struct sufs_libfs_mnode*
sufs_libfs_mnode_dir_exists(struct sufs_libfs_mnode *mnode, char *name)
{
    unsigned long ret = 0;

    if (strcmp(name, ".") == 0)
        return mnode;

    if (strcmp(name, "..") == 0)
        return sufs_libfs_mnode_array[mnode->parent_mnum];

    sufs_libfs_file_enter_cs(mnode);

    if (sufs_libfs_map_file(mnode, 0) != 0)
    {
#if 0
        printf("Failed at sufs_libfs_map_file!\n");
#endif
        goto out;
    }

    sufs_libfs_chainhash_lookup(&mnode->data.dir_data.map_, name, SUFS_NAME_MAX,
            &ret, NULL);
out:
    sufs_libfs_file_exit_cs(mnode);
    return sufs_libfs_mnode_array[ret];
}

static inline void sufs_libfs_mnode_dir_force_delete(struct sufs_libfs_mnode *m)
{
    sufs_libfs_chainhash_forced_remove_and_kill(&m->data.dir_data.map_);
}


/* return the inserted index */
struct sufs_fidx_entry *
sufs_libfs_mnode_file_index_append(struct sufs_libfs_mnode *m,
        unsigned long addr)
{
    struct sufs_fidx_entry * ret = NULL;
    struct sufs_fidx_entry * idx = NULL;

    if (m->index_end == NULL)
    {
        unsigned long block = 0, vaddr = 0;
        sufs_libfs_new_index_blocks(&sufs_libfs_sb, m, &block, 1);

        if (!block)
            return NULL;

        vaddr = sufs_libfs_block_to_virt_addr(block);

        m->index_start = m->index_end = (struct sufs_fidx_entry *) vaddr;

        m->inode->offset = sufs_libfs_block_to_offset(block);
    }

    idx = m->index_end;

#if 0
    printf("m->index_end is %lx\n", (unsigned long) m->index_end);
#endif

    if (likely(sufs_is_norm_fidex(idx)))
    {
        ret = idx;

#if 0
    printf("self: %lx, idx is %lx\n", pthread_self(), (unsigned long) idx);
    printf("self: %lx, idx->offset is %lx\n", pthread_self(), (unsigned long) idx->offset);
#endif
        idx->offset = sufs_libfs_virt_addr_to_offset(addr);
        idx++;
    }
    else
    {
        unsigned long block = 0;
        struct sufs_fidx_entry * old_idx = NULL;

        sufs_libfs_new_index_blocks(&sufs_libfs_sb, m, &block, 1);

        if (!block)
        {
            fprintf(stderr, "alloc index block failed!\n");
            return NULL;
        }

        old_idx = idx;

        idx = (struct sufs_fidx_entry*) sufs_libfs_block_to_virt_addr(block);
        idx->offset = sufs_libfs_virt_addr_to_offset(addr);
        ret = idx;
        idx++;

        /*
         * Mark old_idx as the last step to guarantee atomicity
         * No need for barrier since we assume TSO
         */

        old_idx->offset = sufs_libfs_block_to_offset(block);
    }

    m->index_end = idx;

    return ret;
}

void sufs_libfs_mnode_file_resize_append(struct sufs_libfs_mnode *m, u64 newsize,
        unsigned long addr)
{
    u64 oldsize = m->data.file_data.size_;
    struct sufs_fidx_entry * idx = NULL;

    assert(
            FILE_BLOCK_ROUND_UP(oldsize) / SUFS_FILE_BLOCK_SIZE + 1==
                    FILE_BLOCK_ROUND_UP(newsize) / SUFS_FILE_BLOCK_SIZE);

    idx = sufs_libfs_mnode_file_index_append(m, addr);


    sufs_libfs_mnode_file_fill_index(m,
            (FILE_BLOCK_ROUND_UP(newsize) / SUFS_FILE_BLOCK_SIZE - 1),
            (unsigned long) idx);

    m->data.file_data.size_ = newsize;
}

static void sufs_libfs_mnode_file_unmap(struct sufs_libfs_mnode *m)
{
    struct sufs_fidx_entry *idx = m->index_start;
    unsigned long old_idx = 0;
    unsigned long block = 0;

    if (idx == NULL)
        goto out;

    while (idx->offset != 0)
    {
        if (likely(sufs_is_norm_fidex(idx)))
        {
            block = sufs_libfs_offset_to_block(idx->offset);

            sufs_libfs_block_clear_owned(block);

            idx++;
        }
        else
        {
            old_idx = (unsigned long) idx;

            idx = (struct sufs_fidx_entry*) sufs_libfs_offset_to_virt_addr(
                    idx->offset);

            block = sufs_libfs_virt_addr_to_block(old_idx);

            sufs_libfs_block_clear_owned(block);
        }
    }

    old_idx = (unsigned long) idx;

    idx = (struct sufs_fidx_entry*) sufs_libfs_offset_to_virt_addr(
            idx->offset);

    block = sufs_libfs_virt_addr_to_block(old_idx);

    sufs_libfs_block_clear_owned(block);

out:
    if (m->type == SUFS_FILE_TYPE_REG)
    {
        sufs_libfs_mnode_file_free_page(m);
    }
    else
    {
        sufs_libfs_mnode_dir_force_delete(m);
        sufs_libfs_mnode_dir_free(m);
    }
}

/*
 * if keep_first, do not remove the first offset page
 * otherwise, remove everything
 */

void sufs_libfs_mnode_file_index_delete( struct sufs_fidx_entry *idx,
        int keep_first)
{
    int first = 1, persist = 0;

    unsigned long old_idx = 0;
    unsigned long block = 0;

    if (idx == NULL)
        return;

    while (idx->offset != 0)
    {
        if (likely(sufs_is_norm_fidex(idx)))
        {
            block = sufs_libfs_offset_to_block(idx->offset);

            if (sufs_libfs_is_block_owned(block))
            {
                sufs_libfs_free_data_blocks(&sufs_libfs_sb, block,
                        SUFS_FILE_BLOCK_PAGE_CNT);
            }

            idx->offset = 0;

            /* persist the first write to idx->offset */
            if (!persist)
            {
                sufs_libfs_clwb_buffer(&idx->offset, sizeof(idx->offset));
                sufs_libfs_sfence();
                persist = 1;
            }


            idx++;
        }
        else
        {
            unsigned long nidx = 0;

            old_idx = (unsigned long) idx;

            nidx = (unsigned long) sufs_libfs_offset_to_virt_addr(idx->offset);

            idx->offset = 0;

            /* persist the first write to idx->offset */
            if (!persist)
            {
                sufs_libfs_clwb_buffer(&idx->offset, sizeof(idx->offset));
                sufs_libfs_sfence();
                persist = 1;
            }

            idx = (struct sufs_fidx_entry*) nidx;

            if (!keep_first || (keep_first && !first))
            {
                block = sufs_libfs_virt_addr_to_block(old_idx);

                if (sufs_libfs_is_block_owned(block))
                {
                    sufs_libfs_free_index_block(&sufs_libfs_sb, block);
                }

                first = 0;
            }
        }
    }

    /* remove the remaining one page*/
    old_idx = (unsigned long) idx;

    if (!keep_first || (keep_first && !first))
    {
        block = sufs_libfs_virt_addr_to_block(old_idx);

        if (sufs_libfs_is_block_owned(block))
        {
            sufs_libfs_free_index_block(&sufs_libfs_sb, block);
        }

        first = 0;
    }

    return;
}

void sufs_libfs_mnode_file_truncate_zero(struct sufs_libfs_mnode *m)
{
    struct sufs_fidx_entry *idx = NULL;

    sufs_libfs_mnode_file_index_delete(m->index_start, 1);

    m->index_end = m->index_start;

    idx = m->index_start;

    if (idx != NULL)
        idx->offset = 0;

    m->data.file_data.size_ = 0;
    sufs_libfs_mnode_file_free_page(m);
}

void sufs_libfs_mnode_file_delete(struct sufs_libfs_mnode *m)
{
    if (sufs_libfs_is_inode_allocated(m->ino_num))
    {
        sufs_libfs_mnode_file_index_delete(m->index_start, 0);
    }

    if (m->type == SUFS_FILE_TYPE_REG)
    {
        sufs_libfs_mnode_file_free_page(m);
    }
    else
    {
        sufs_libfs_mnode_dir_free(m);
    }
}

void sufs_libfs_do_mnode_stat(struct sufs_libfs_mnode *m, struct stat *st)
{
    unsigned int stattype = 0;
    int type = m->type;
    switch (type)
    {
        case SUFS_FILE_TYPE_REG:
            stattype = S_IFREG;
            break;
        case SUFS_FILE_TYPE_DIR:
            stattype = S_IFDIR;
            break;
        default:
            fprintf(stderr, "Unknown type %d\n", type);
    }

    st->st_dev = 0;
    st->st_ino = m->ino_num;

    st->st_mode = stattype | m->inode->mode;


    st->st_nlink = 1;

    st->st_uid = m->inode->uid;
    st->st_gid = m->inode->gid;
    st->st_rdev = 0;

    st->st_size = 0;

    if (sufs_libfs_file_is_mapped(m))
    {
        if (type == SUFS_FILE_TYPE_REG)
            st->st_size = sufs_libfs_mnode_file_size(m);
    }
    else
    {
        st->st_size = (1024 * 1024 * 1024);
        /* st->st_size = m->inode->size; */
    }

    st->st_blksize = st->st_size / SUFS_PAGE_SIZE;
    st->st_blocks = st->st_size / 512;

    /* TODO: get from inode */
    memset(&st->st_atim, 0, sizeof(struct timespec));
    memset(&st->st_mtim, 0, sizeof(struct timespec));
    memset(&st->st_ctim, 0, sizeof(struct timespec));

}

int sufs_libfs_mnode_stat(struct sufs_libfs_mnode *m, struct stat *st)
{
    int ret = -1;

    struct sufs_libfs_mnode * parent = sufs_libfs_mnode_array[m->parent_mnum];

    sufs_libfs_file_enter_cs(parent);

    if (sufs_libfs_map_file(parent, 0) != 0)
        goto out;

    sufs_libfs_do_mnode_stat(m, st);
    ret = 0;

out:
    sufs_libfs_file_exit_cs(parent);

    return ret;
}
