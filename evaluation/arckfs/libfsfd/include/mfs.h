#ifndef SUFS_LIBFS_MFS_H_
#define SUFS_LIBFS_MFS_H_

#include <stdatomic.h>

#include "../../include/libfs_config.h"
#include "types.h"
#include "balloc.h"
#include "ialloc.h"

extern u64 sufs_root_mnum;

extern struct sufs_libfs_mnode *sufs_libfs_root_dir;

extern atomic_char *sufs_libfs_inode_mapped_attr;

extern atomic_char * sufs_libfs_inode_has_index;

extern struct sufs_libfs_chainhash sufs_libfs_dir_map;

extern pthread_spinlock_t * sufs_libfs_inode_map_lock;

struct sufs_libfs_mnode* sufs_libfs_namex(struct sufs_libfs_mnode *cwd,
        char *path, bool nameiparent, char *name);

static inline struct sufs_libfs_mnode* sufs_libfs_namei(
        struct sufs_libfs_mnode *cwd, char *path)
{
    /* TODO: can this buf moved */
    char buf[SUFS_NAME_MAX];
    return sufs_libfs_namex(cwd, path, false, buf);
}

static inline struct sufs_libfs_mnode* sufs_libfs_nameiparent(
        struct sufs_libfs_mnode *cwd, char *path, char *buf)
{
    return sufs_libfs_namex(cwd, path, true, buf);
}

/* BUG: Using a bit is wrong, should make it use a counter */
static inline void sufs_libfs_file_enter_cs(struct sufs_libfs_mnode *m)
{
    /* sufs_libfs_bm_set_bit((char*) SUFS_LEASE_RING_ADDR, m->ino_num); */
}

static inline void sufs_libfs_file_exit_cs(struct sufs_libfs_mnode *m)
{
    /* sufs_libfs_bm_clear_bit((char*) SUFS_LEASE_RING_ADDR, m->ino_num); */
}

static inline int sufs_libfs_file_is_mapped(struct sufs_libfs_mnode *m)
{
#if 0
    if (m->ino_num != 2 && m->ino_num !=3 && !sufs_libfs_is_inode_allocated(m->ino_num))
    {
        printf("id: %lx, Not what we allocated: %d\n", pthread_self(), m->ino_num);
        while (1);
    }
#endif
#if 0
    printf("sufs_libfs_bm_test_bit: inode: %d, val: %d\n", m->ino_num,
            sufs_libfs_bm_test_bit((char*) SUFS_MAPPED_RING_ADDR, m->ino_num));
#endif
    return (sufs_libfs_is_inode_allocated(m->ino_num) ||
           (sufs_libfs_bm_test_bit((atomic_char*) SUFS_MAPPED_RING_ADDR, m->ino_num)
            && sufs_libfs_bm_test_bit(sufs_libfs_inode_has_index, m->ino_num)
            ));
}

static inline void sufs_libfs_file_set_writable(struct sufs_libfs_mnode * m)
{
    sufs_libfs_bm_set_bit(sufs_libfs_inode_mapped_attr, m->ino_num);
}

static inline int sufs_libfs_file_mapped_writable(struct sufs_libfs_mnode *m)
{
    return (sufs_libfs_is_inode_allocated(m->ino_num) ||
            sufs_libfs_bm_test_bit(sufs_libfs_inode_mapped_attr, m->ino_num));
}

static inline void sufs_libfs_lock_file_mapping(struct sufs_libfs_mnode * m)
{
    long index = m->ino_num % SUFS_LIBFS_FILE_MAP_LOCK_SIZE;
    pthread_spin_lock(&(sufs_libfs_inode_map_lock[index]));
}

static inline void sufs_libfs_unlock_file_mapping(struct sufs_libfs_mnode * m)
{
    long index = m->ino_num % SUFS_LIBFS_FILE_MAP_LOCK_SIZE;
    pthread_spin_unlock(&(sufs_libfs_inode_map_lock[index]));
}

void sufs_libfs_mfs_init(void);

void sufs_libfs_mfs_fini(void);

void sufs_libfs_mfs_add_mapped_inode(int ino);
void sufs_libfs_mfs_unmap_mapped_inode(void);

void sufs_libfs_fs_init();

void sufs_libfs_fs_fini(void);

s64 sufs_libfs_readm(struct sufs_libfs_mnode *m, char *buf, u64 start,
        u64 nbytes);

s64 sufs_libfs_writem(struct sufs_libfs_mnode *m, char *buf, u64 start,
        u64 nbytes);

void sufs_libfs_flush_file_index(struct sufs_fidx_entry * start,
        struct sufs_fidx_entry * end);

int sufs_libfs_truncatem(struct sufs_libfs_mnode *m, off_t length);

int sufs_libfs_map_file(struct sufs_libfs_mnode *m, int writable, char * path);

int sufs_libfs_do_map_file(struct sufs_libfs_mnode *m, int writable);

int sufs_libfs_upgrade_file_map(struct sufs_libfs_mnode *m);

void sufs_libfs_file_build_index(struct sufs_libfs_mnode *m);

#endif /* SUFS_MFS_H_ */
