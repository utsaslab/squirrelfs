#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include "../include/libfs_config.h"
#include "../include/ioctl.h"
#include "orig_syscall.h"
#include "cmd.h"

static int dev_fd = 0;

void sufs_libfs_cmd_init(void)
{
    int fd = 0;

    fd = sufs_libfs_orig_open(SUFS_DEV_PATH, O_RDWR);

    if (fd == -1)
    {
        fprintf(stderr, "Cannot open %s\n", SUFS_DEV_PATH);
        abort();
    }
    else
    {
        dev_fd = fd;
    }
}

void sufs_libfs_cmd_fini(void)
{
    if (dev_fd != 0)
        sufs_libfs_orig_close(dev_fd);
}

int sufs_libfs_cmd_mount(void)
{
    return syscall(SYS_ioctl, dev_fd, SUFS_CMD_MOUNT, NULL);
}

int sufs_libfs_cmd_umount(void)
{
    return syscall(SYS_ioctl, dev_fd, SUFS_CMD_UMOUNT, SUFS_MOUNT_ADDR);
}


int sufs_libfs_cmd_map_file(int ino, int writable, unsigned long *index_offset)
{
    int ret = 0;
    struct sufs_ioctl_map_entry entry;

    entry.inode = ino;
    entry.perm = writable;
#if 0
    printf("ino is %d\n", ino);
#endif

    ret = syscall(SYS_ioctl, dev_fd, SUFS_CMD_MAP, &entry);

    if (ret == 0)
        (*index_offset) = entry.index_offset;

    return ret;
}

int sufs_libfs_cmd_unmap_file(int ino)
{
    struct sufs_ioctl_map_entry entry;

    entry.inode = ino;

    return syscall(SYS_ioctl, dev_fd, SUFS_CMD_UNMAP, &entry);
}

int sufs_libfs_cmd_alloc_inodes(int *ino, int *num, int cpu)
{
    int ret = 0;
    struct sufs_ioctl_inode_alloc_entry entry;

    entry.num = (*num);
    entry.cpu = cpu;

    ret = (int) syscall(SYS_ioctl, dev_fd, SUFS_CMD_ALLOC_INODE, &entry);

    (*ino) = entry.inode;
    (*num) = entry.num;

#if 0
    printf("get inodes, start: %d, end: %d\n", entry.inode, entry.inode +
            entry.num - 1);
#endif

    return ret;
}

int sufs_libfs_cmd_free_inodes(int ino, int num)
{
    struct sufs_ioctl_inode_alloc_entry entry;

    entry.inode = ino;
    entry.num = num;

#if 0
    printf("free inodes, start: %d, end: %d\n", entry.inode, entry.inode +
            entry.num - 1);
#endif

    return syscall(SYS_ioctl, dev_fd, SUFS_CMD_FREE_INODE, &entry);

}

int sufs_libfs_cmd_get_sys_info(int *pm_nodes,
        struct sufs_libfs_pm_node_info *info, int *sockets,
        int *cpus_per_socket, int * dele_ring_per_node)
{
    struct sufs_ioctl_sys_info_entry entry;
    int ret = 0;

    entry.raddr = info;

    ret = syscall(SYS_ioctl, dev_fd, SUFS_CMD_GET_PMNODES_INFO, &entry);

    if (ret == 0)
    {
        (*pm_nodes) = entry.pmnode_num;
        (*sockets) = entry.sockets;
        (*cpus_per_socket) = entry.cpus_per_socket;
        (*dele_ring_per_node) = entry.dele_ring_per_node;
    }

    return ret;
}

int sufs_libfs_cmd_alloc_blocks(unsigned long *block, unsigned long *num,
        int cpu, int pmnode)
{
    int ret = 0;
    struct sufs_ioctl_block_alloc_entry entry;

    entry.num = (*num);
    entry.cpu = cpu;
    entry.pmnode = pmnode;

    ret = (int) syscall(SYS_ioctl, dev_fd, SUFS_CMD_ALLOC_BLOCK, &entry);

    (*block) = entry.block;
    (*num) = entry.num;

    return ret;
}

int sufs_libfs_cmd_free_blocks(unsigned long block, unsigned long num)
{
    struct sufs_ioctl_block_alloc_entry entry;

    entry.block = block;
    entry.num = num;

    return syscall(SYS_ioctl, dev_fd, SUFS_CMD_FREE_BLOCK, &entry);
}

int sufs_libfs_cmd_chown(int inode, int uid, int gid,
        unsigned long inode_offset)
{
    struct sufs_ioctl_chown_entry entry;

    entry.inode = inode;
    entry.owner = uid;
    entry.group = gid;
    entry.inode_offset = inode_offset;

    return syscall(SYS_ioctl, dev_fd, SUFS_CMD_CHOWN, &entry);
}

int sufs_libfs_cmd_chmod(int inode, unsigned int mode,
        unsigned long inode_offset)
{
    struct sufs_ioctl_chmod_entry entry;

    entry.inode = inode;
    entry.mode = mode;
    entry.inode_offset = inode_offset;

    return syscall(SYS_ioctl, dev_fd, SUFS_CMD_CHMOD, &entry);
}

