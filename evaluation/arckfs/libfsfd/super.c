#include <stdio.h>
#include <stdlib.h>

#include "../include/libfs_config.h"
#include "super.h"
#include "cmd.h"

struct sufs_libfs_super_block sufs_libfs_sb;

int sufs_libfs_delegation = 0;

void sufs_libfs_init_super_block(struct sufs_libfs_super_block * sb)
{
    if (sufs_libfs_cmd_get_sys_info(&(sb->pm_nodes),
            sb->pm_node_info, &(sb->sockets), &(sb->cpus_per_socket),
            &(sb->dele_ring_per_node)) < 0)
    {
        fprintf(stderr, "super block init failed!\n");
        abort();
    }

    sb->start_addr = SUFS_MOUNT_ADDR;

    if (sb->dele_ring_per_node == 0)
    {
        sufs_libfs_delegation = 0;
    }
    else
    {
        sufs_libfs_delegation = 1;
    }

#if 0
    printf("sb->pm_nodes: %d, sb->sockets: %d, sb->cpus_per_socket: %d\n",
            sb->pm_nodes, sb->sockets, sb->cpus_per_socket);
#endif
}
