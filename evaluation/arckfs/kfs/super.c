#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/dax.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/memory.h>

#include "../include/kfs_config.h"
#include "../include/common_inode.h"
#include "super.h"
#include "mmap.h"
#include "ring.h"
#include "inode.h"
#include "tgroup.h"
#include "balloc.h"
#include "agent.h"
#include "ring_buffer.h"

struct sufs_super_block sufs_sb;
int sufs_kfs_delegation = 0;

void sufs_init_sb(void)
{
    int cpus = num_online_cpus();
    memset(&sufs_sb, 0, sizeof(struct sufs_super_block));

    sufs_sb.sockets = num_online_nodes();
    sufs_sb.cpus_per_socket = cpus / sufs_sb.sockets;

    sufs_sb.dele_ring_per_node = sufs_kfs_dele_thrds;
}

void sufs_sb_update_one_dev(int node, unsigned long virt_addr,
        unsigned long size_in_bytes)
{
    if (sufs_sb.start_virt_addr == 0 || virt_addr < sufs_sb.start_virt_addr)
    {
        sufs_sb.start_virt_addr = virt_addr;
        sufs_sb.head_node = node;
    }

    if (sufs_sb.end_virt_addr ==  0 ||
            virt_addr + size_in_bytes - 1 > sufs_sb.end_virt_addr)
    {
        sufs_sb.end_virt_addr = virt_addr + size_in_bytes - 1;
    }
}

void sufs_sb_update_devs(struct sufs_dev_arr * sufs_dev_arr)
{
    int i = 0;

    sufs_sb.pm_nodes = sufs_dev_arr->num;

    for (i = 0; i < sufs_sb.pm_nodes; i++)
    {
        unsigned long end_vaddr = 0;

        end_vaddr = sufs_dev_arr->start_virt_addr[i] +
                sufs_dev_arr->size_in_bytes[i] - 1;

        sufs_sb.pm_node_info[i].start_block =
                sufs_kfs_virt_addr_to_block(sufs_dev_arr->start_virt_addr[i]);

        sufs_sb.pm_node_info[i].end_block =
                sufs_kfs_virt_addr_to_block(end_vaddr);
    }

}

/* init file system related fields */

/*
 * One page superblock
 * Multiple pages for shadow inode
 * One extra page for root inode
 */
static void sufs_sb_fs_init(void)
{
    unsigned long sinode_size = 0;
    int i = 0;

    sinode_size = SUFS_MAX_INODE_NUM * sizeof(struct sufs_shadow_inode);

    sufs_sb.sinode_start = (struct sufs_shadow_inode *)
                               (sufs_sb.start_virt_addr + SUFS_SUPER_PAGE_SIZE);

    for (i = 0; i < SUFS_MAX_INODE_NUM; i++)
    {
        sufs_sb.sinode_start[i].file_type = SUFS_FILE_TYPE_NONE;
    }


    sufs_sb.head_reserved_blocks = (sinode_size >> PAGE_SHIFT) + 2;

    sufs_init_inode_free_list(&sufs_sb);

    sufs_init_block_free_list(&sufs_sb, 0);
}


long sufs_mount(void)
{
    struct file * file;

    long ret = 0;

    struct sufs_tgroup * tgroup = NULL;

    tgroup = sufs_kfs_pid_to_tgroup(current->tgid, 1);

    if (!tgroup)
    {
        printk("Cannot allocate tgroup during mount!\n");
        return -ENOMEM;
    }

    /* TODO: Think of the permission of this file ... */
    file = filp_open(SUFS_DEV_PATH, O_RDWR, 0);

    if (IS_ERR(file))
    {
        ret = PTR_ERR(file);
        printk("Open: %s failed with error : %d\n", SUFS_DEV_PATH, (int) ret);

        return ret;
    }

    /*
     * Reserving a large VMA to contain all the PM devices in the process
     * address space.
     *
     * Protection does not matter; This VMA does not support on demand paging.
     *
     */
    ret = vm_mmap(file, SUFS_MOUNT_ADDR, sufs_sb.tot_bytes,
            PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, 0);

    filp_close(file, NULL);

    if (IS_ERR_VALUE(ret))
        return ret;

    ret = sufs_kfs_create_ring(tgroup);

    if (IS_ERR_VALUE(ret))
    {
        vm_munmap(SUFS_MOUNT_ADDR, sufs_sb.tot_bytes);
        return ret;
    }

    tgroup->mount_vma = find_vma(current->mm, SUFS_MOUNT_ADDR);
    if (tgroup->mount_vma == NULL)
    {
        printk("Cannot find the mount vma!\n");
        return -ENOMEM;
    }

    sufs_kfs_mm = current->mm;

    return ret;
}

int sufs_umount(unsigned long addr)
{
    int ret = 0;
    struct sufs_tgroup * tgroup = NULL;

    ret = vm_munmap(addr, sufs_sb.tot_bytes);

    tgroup = sufs_kfs_pid_to_tgroup(current->tgid, 1);

    if (!tgroup)
    {
        printk("Cannot find the tgroup with pid during umount: %d\n",
                current->tgid);

        return -ENOMEM;
    }

    sufs_kfs_delete_ring(tgroup);

    return ret;
}

/* TODO: add a macro to enable/disable debugging code */
long sufs_debug_read()
{
    return 0;
}

static void sufs_init_root_inode(void)
{
    unsigned long data_block = sufs_sb.head_reserved_blocks - 1;
    unsigned long vaddr = sufs_kfs_block_to_virt_addr(data_block);

    memset((void *) vaddr, 0, PAGE_SIZE);

    sufs_kfs_set_inode(SUFS_ROOT_INODE, SUFS_FILE_TYPE_DIR,
            SUFS_ROOT_PERM, 0, 0, sufs_kfs_block_to_offset(data_block));
}


/* Format the file system */
int sufs_fs_init(void)
{
    int ret = 0;

    if ((ret = sufs_kfs_init_tgroup()) != 0)
        goto fail;

    if ((ret = sufs_init_rangenode_cache()) != 0)
        goto fail_rangenode;

    if ((ret = sufs_alloc_inode_free_list(&sufs_sb)) != 0)
        goto fail_inode_free_list;

    if ((ret = sufs_alloc_block_free_lists(&sufs_sb)) != 0)
        goto fail_block_free_lists;

    sufs_sb_fs_init();

    sufs_init_root_inode();

    if (sufs_kfs_agent_init)
    {
        sufs_kfs_agents_fini();
        sufs_kfs_fini_ring_buffers(sufs_sb.pm_nodes);
    }

    sufs_kfs_init_ring_buffers(sufs_sb.pm_nodes);

    sufs_kfs_init_agents(&sufs_sb);

    sufs_kfs_agent_init = 1;

    return 0;

fail_block_free_lists:
    sufs_free_inode_free_list(&sufs_sb);
fail_inode_free_list:
    sufs_free_rangenode_cache();

fail_rangenode:
    sufs_kfs_fini_tgroup();

fail:
    return ret;

}

void sufs_fs_fini(void)
{
    if (sufs_kfs_agent_init)
    {
        sufs_kfs_agents_fini();

        sufs_kfs_fini_ring_buffers(sufs_sb.pm_nodes);
    }

    sufs_delete_block_free_lists(&sufs_sb);

    sufs_free_inode_free_list(&sufs_sb);

    sufs_free_rangenode_cache();

    sufs_kfs_fini_tgroup();

    return;
}

