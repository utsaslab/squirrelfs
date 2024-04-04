#ifndef SUFS_LIBFS_CMD_H_
#define SUFS_LIBFS_CMD_H_

#include "../../include/libfs_config.h"
#include "super.h"

void sufs_libfs_cmd_init(void);
void sufs_libfs_cmd_fini(void);

int sufs_libfs_cmd_mount(void);
int sufs_libfs_cmd_umount(void);

int sufs_libfs_cmd_map_file(int ino, int writable, unsigned long *index_offset);
int sufs_libfs_cmd_unmap_file(int ino);

int sufs_libfs_cmd_alloc_inodes(int *ino, int *num, int cpu);
int sufs_libfs_cmd_free_inodes(int ino, int num);

int sufs_libfs_cmd_get_sys_info(int *pm_nodes,
        struct sufs_libfs_pm_node_info *info, int *sockets,
        int *cpus_per_socket, int * dele_ring_per_node);

int sufs_libfs_cmd_alloc_blocks(unsigned long *block, unsigned long *num,
        int cpu, int pmnode);
int sufs_libfs_cmd_free_blocks(unsigned long block, unsigned long num);

int sufs_libfs_cmd_chown(int inode, int uid, int gid,
        unsigned long inode_offset);

int sufs_libfs_cmd_chmod(int inode, unsigned int mode,
        unsigned long inode_offset);

#endif
