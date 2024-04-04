#ifndef SUFS_GLOBAL_LIBFS_CONFIG_H_
#define SUFS_GLOBAL_LIBFS_CONFIG_H_

/* LibFS specific config */

#include "config.h"

#define SUFS_LIBFS_DIR_INIT_HASH_IDX   0

#define SUFS_LIBFS_GDIR_INIT_HASH_IDX  14

#define SUFS_LIBFS_DIR_REHASH_FACTOR   2

/* maximum open files per process */
#define SUFS_LIBFS_MAX_FD 1024

/* How many inodes we get from the kernel when one CPU runs out of inode */
#define SUFS_LIBFS_INODE_CHUNK 10000

/* How many blocks we get from the kernel when one CPU runs out of blocks */

// #define SUFS_LIBFS_INIT_BLOCK_CHUNK 64
#define SUFS_LIBFS_INIT_BLOCK_CHUNK 25600

#define SUFS_LIBFS_EXTRA_BLOCK_CHUNK (256 * 16)
// #define SUFS_LIBFS_EXTRA_BLOCK_CHUNK 102400 // 400MB

#define SUFS_ROOT_PATH        "/sufs/"

#define SUFS_LIBFS_BASE_FD    (1024 * 1024)

/*
 * TODO: Add APIs to retrieve the below information from the kernel or
 * environment variable
 */
#define SUFS_LIBFS_CPUS       224

/* Maximum number of mapped file in LIBFS */
#define SUFS_MAX_MAP_FILE     4096

/* BRAVO config */
#define SUFS_LIBFS_RL_TABLE_SIZE       (1024 * 1024)

/* Use a prime number to reduce the chance of contention */
#define SUFS_LIBFS_RL_NUM_SLOT         (1048573)

#define SUFS_LIBFS_BRAVO_N             9

/* stock pthread spin_lock */
#define SUFS_INODE_RW_LOCK_SPIN        1
/* bravo reader/writer lock */
#define SUFS_INODE_RW_LOCK_BRAVO       2

#define SUFS_INODE_RW_LOCK             SUFS_INODE_RW_LOCK_SPIN

/* Range lock config */

/* INIT file size: 4KB */
#define SUFS_LIBFS_SEGMENT_INIT_COUNT  1

#define SUFS_LIBFS_SEGMENT_SIZE_BITS   12

#define SUFS_LIBFS_RANGE_LOCK          0

/* Misc */

#define SUFS_LIBFS_FILE_MAP_LOCK_SIZE    4096

#define SUFS_LIBFS_STAT                  0

#endif
