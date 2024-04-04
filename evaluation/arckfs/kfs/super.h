#ifndef SUFS_KFS_SUPER_H_
#define SUFS_KFS_SUPER_H_

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/dax.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/memory.h>

#include "../include/kfs_config.h"
#include "dev_dax.h"
#include "lease.h"

#define SUFS_KFS_UNOWNED              0
#define SUFS_KFS_WRITE_OWNED          1
#define SUFS_KFS_READ_OWNED           2

struct sufs_pm_node_info {
    /* [start_block, end_block] */
    unsigned long start_block, end_block;
};

/* Confirm to the order of kstat */
struct sufs_shadow_inode
{
    char file_type;
    unsigned int  mode;
    unsigned int  uid;
    unsigned int  gid;

    /* Where to find the first index page */
    unsigned long index_offset;

    struct sufs_kfs_lease lease;
    /* TODO: add pad space to make it cache block aligned */
};



/* Make it in DRAM as of now ... */
struct sufs_super_block
{
    int pm_nodes;
    struct sufs_pm_node_info pm_node_info[SUFS_PM_MAX_INS];
    int head_node;

    /* The starting virtual address of the pmem device array
     *
     * We do not store the real address of data structures in PM, but
     * instead stores the offset from start_virt_addr, since rebooting
     * the machine may change the absolute address
     *
     * In theory, the virtual address gap between different PM will also
     * change... Our experience with Odinfs is that this rarely happens and
     * should be OK for a research prototype
     */

    unsigned long start_virt_addr;
    unsigned long end_virt_addr;

    unsigned long tot_bytes;

    /* Number of sockets and the number of CPUs in each socket */
    int sockets;

    int cpus_per_socket;

    int dele_ring_per_node;

    /* Free list of each CPU, used for managing PM space */
    struct sufs_free_list *free_lists;

    struct sufs_shadow_inode * sinode_start;

    /* Free inode of each CPU, used for allocating and freeing inode space */
    struct sufs_inode_free_list * inode_free_lists;

    unsigned long head_reserved_blocks;
};

extern struct sufs_super_block sufs_sb;
extern int sufs_kfs_delegation;

void sufs_init_sb(void);

void sufs_sb_update_one_dev(int node, unsigned long virt_addr,
        unsigned long size_in_bytes);

void sufs_sb_update_devs(struct sufs_dev_arr * sufs_dev_arr);

long sufs_mount(void);

int sufs_umount(unsigned long addr);

long sufs_debug_read(void);

int sufs_fs_init(void);

void sufs_fs_fini(void);

#endif /* KFS_SUPER_H_ */
