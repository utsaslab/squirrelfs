#ifndef SUFS_LIBFS_SUPER_H_
#define SUFS_LIBFS_SUPER_H_

#include "../../include/libfs_config.h"


struct sufs_libfs_pm_node_info {
    /* [start_block, end_block] */
    unsigned long start_block, end_block;
};

struct sufs_libfs_super_block
{
    int pm_nodes;
    struct sufs_libfs_pm_node_info pm_node_info[SUFS_PM_MAX_INS];

    /* Number of sockets and the number of CPUs in each socket */
    int sockets;

    int cpus_per_socket;

    unsigned long start_addr;

    /* Number of delegation ring per node */
    int dele_ring_per_node;

    /* Free list of each CPU, used for managing PM space */
    struct sufs_libfs_free_list *free_lists;

    /* Free inode of each CPU, used for allocating and freeing inode space */
    struct sufs_libfs_inode_free_list * inode_free_lists;

    /* the starting address of the journal*/
    unsigned long journal_addr;
};

extern struct sufs_libfs_super_block sufs_libfs_sb;
extern int sufs_libfs_delegation;

static inline
unsigned long sufs_libfs_offset_to_virt_addr(unsigned long offset)
{
    return offset + SUFS_MOUNT_ADDR;
}

static inline
unsigned long sufs_libfs_virt_addr_to_offset(unsigned long addr)
{
    return addr - SUFS_MOUNT_ADDR;
}

void sufs_libfs_init_super_block(struct sufs_libfs_super_block * sb);


#endif /* INCLUDE_SUPER_H_ */
