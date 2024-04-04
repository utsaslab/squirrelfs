#ifndef SUFS_GLOBAL_IOCTL_H_
#define SUFS_GLOBAL_IOCTL_H_

/* ioctl commands */
/* I *refuse* to learn the stupid (and ugly) IOCTL macros */
#define SUFS_CMD_MOUNT         0x1000
#define SUFS_CMD_UMOUNT        0x1001
#define SUFS_CMD_MAP           0x1002
#define SUFS_CMD_UNMAP         0x1003

#define SUFS_CMD_ALLOC_INODE   0x1004
#define SUFS_CMD_FREE_INODE    0x1005

#define SUFS_CMD_GET_PMNODES_INFO  0x1006
#define SUFS_CMD_ALLOC_BLOCK      0x1007
#define SUFS_CMD_FREE_BLOCK       0x1008

#define SUFS_CMD_CHOWN            0x1009
#define SUFS_CMD_CHMOD            0x100a

/* For debugging */
#define SUFS_CMD_DEBUG_READ    0x2000
#define SUFS_CMD_DEBUG_INIT    0x2001

struct sufs_ioctl_map_entry
{
    int inode;
    int perm;
    unsigned long index_offset;
};


struct sufs_ioctl_inode_alloc_entry
{
    int inode, num, cpu;
};

struct sufs_ioctl_sys_info_entry
{
    int pmnode_num;
    int sockets;
    int cpus_per_socket;
    int dele_ring_per_node;
    void * raddr;
};

struct sufs_ioctl_block_alloc_entry
{
    unsigned long block, num;
    int cpu, pmnode;
};

struct sufs_ioctl_chown_entry
{
    int inode, owner, group;

    unsigned long inode_offset;
};

struct sufs_ioctl_chmod_entry
{
    int inode;
    unsigned int mode;
    unsigned long inode_offset;
};


#endif
