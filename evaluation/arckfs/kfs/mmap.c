#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/cred.h>

#include "../include/kfs_config.h"
#include "../include/ioctl.h"
#include "../include/common_inode.h"
#include "mmap.h"
#include "tgroup.h"
#include "file.h"
#include "inode.h"
#include "util.h"

static vm_fault_t sufs_page_fault(struct vm_fault *vmf)
{
    /* Should not trigger page fault */
    printk("Enter sufs page fault: address: %lx!\n",
            vmf->address);
    return VM_FAULT_SIGBUS;
}

static const struct vm_operations_struct sufs_vm_ops = { .fault =
        sufs_page_fault, };

int sufs_kfs_mmap(struct file *filp, struct vm_area_struct *vma)
{
    vma->vm_ops = &sufs_vm_ops;

    /*  Makes vmf_insert_pfn_prot happy */
    /*  TODO: validate whether VM_PFNMAP has any side effect or not */
    vm_flags_set(vma, VM_PFNMAP);

    return 0;
}

/* Simplified permission check, does not consider many factors ... */
static unsigned long sufs_gen_access_perm(struct sufs_shadow_inode *sinode,
        int write)
{
    const struct cred *cred = current_cred();

    unsigned long ret = 0;

    /* owner */
    if (sinode->uid == cred->uid.val)
    {
        if (sinode->mode & S_IRUSR)
        {
            ret |= VM_READ;
            if (write && (sinode->mode & S_IWUSR))
                ret |= VM_WRITE;
        }
    }
    /* group */
    else if (sinode->gid == cred->gid.val)
    {
        if (sinode->mode & S_IRGRP)
        {
            ret |= VM_READ;
            if (write && (sinode->mode & S_IWGRP))
                ret |= VM_WRITE;
        }
    }
    /* others */
    else
    {
        if (sinode->mode & S_IROTH)
        {
            ret |= VM_READ;
            if (write && (sinode->mode & S_IWOTH))
                ret |= VM_WRITE;
        }
    }

    if (ret == 0)
        return ret;
    else
        return (ret | VM_SHARED);
}

void sufs_map_pages(struct vm_area_struct * vma,
        unsigned long vaddr, unsigned long pfn, pgprot_t prop, long count)
{
    vm_fault_t rc;

    long i = 0;

    for (i = 0; i < count; i++)
    {
        if ((rc = vmf_insert_pfn_prot(vma, vaddr, pfn, prop)) != VM_FAULT_NOPAGE)
        {
            printk("insert pfn root failed with vaddr: %lx, pfn: %lx, rc: %x\n",
                    vaddr, pfn, rc);
        }

        vaddr += PAGE_SIZE;
        pfn++;
    }
}


static long sufs_map_file_pages(struct sufs_super_block * sb,
        struct sufs_shadow_inode * sinode, struct vm_area_struct * vma,
        pgprot_t prop)
{
    struct sufs_fidx_entry * idx = NULL;

    unsigned long offset = 0;

    if (sinode->index_offset == 0)
        return 0;

    idx = (struct sufs_fidx_entry * )
                    sufs_kfs_offset_to_virt_addr(sinode->index_offset);

    /* map the first index page */
    offset = sufs_kfs_virt_addr_to_offset((unsigned long) idx);
    sufs_map_pages(vma, SUFS_MOUNT_ADDR + offset,
            sufs_kfs_offset_to_pfn(offset), prop, 1);

    while (idx->offset != 0)
    {
        if (likely(sufs_is_norm_fidex(idx)))
        {
            /* map the data page */
            offset = idx->offset;

            sufs_map_pages(vma, SUFS_MOUNT_ADDR + offset,
                    sufs_kfs_offset_to_pfn(offset), prop, SUFS_FILE_BLOCK_PAGE_CNT);

            idx++;
        }
        else
        {
            /* map the index page */
            idx = (struct sufs_fidx_entry*)
                    sufs_kfs_offset_to_virt_addr(idx->offset);

            offset = sufs_kfs_virt_addr_to_offset((unsigned long) idx);

            sufs_map_pages(vma, SUFS_MOUNT_ADDR + offset,
                    sufs_kfs_offset_to_pfn(offset), prop, 1);

        }
    }

    return 0;
}




/*
 * write != 0, mmaped as read and write,
 * otherwise, mmaped as read
 */
static long sufs_do_mmap_file(struct sufs_super_block * sb, int ino,
        int writable, long * index_offset)
{
    struct sufs_tgroup *tgroup = NULL;

    struct vm_area_struct *vma = NULL;

    struct sufs_shadow_inode *sinode = NULL;

    struct sufs_kfs_lease * lease = NULL;

    long ret = 0;

    unsigned long perm = 0;
    int tgid = 0;

    /* This is quite stupid */
    pgprot_t prop;

    tgid = sufs_kfs_pid_to_tgid(current->tgid, 0);

    tgroup = sufs_kfs_tgid_to_tgroup(tgid);

    if (tgroup == NULL)
    {
        printk("Cannot find the tgroup with pid :%d\n", current->tgid);
        return -ENODEV;
    }

    vma = tgroup->mount_vma;

    if (vma == NULL)
    {
        printk("Cannot find the mapped vma\n");
        return -ENODEV;
    }

    sinode = sufs_find_sinode(ino);

    if (sinode == NULL)
    {
        printk("Cannot find sinode with ino %d\n", ino);
        return -EINVAL;
    }

    lease = &(sinode->lease);

    if (lease == NULL)
    {
        printk("Sinode with empty lease %d\n", ino);
        return -EINVAL;
    }

    perm = sufs_gen_access_perm(sinode, writable);

    if (perm == 0)
    {
        printk("Cannot access file with ino: %d, uid: %d, gid: %d, mode: %d\n",
                ino, sinode->uid, sinode->gid, sinode->mode);
        return -EACCES;
    }

#if 0
    printk("ino is %d, si is %lx, lease is %lx\n", ino,
            (unsigned long )sinode, (unsigned long) lease);
#endif

    if (writable)
    {
        ret = sufs_kfs_acquire_write_lease(ino, lease, tgid);
    }
    else
    {
        ret = sufs_kfs_acquire_read_lease(ino, lease, tgid);
    }

    if (ret < 0)
    {
        printk("Cannot acquire the lease with ino: %d!\n", ino);
        return ret;
    }

    prop = vm_get_page_prot(perm);

    ret = sufs_map_file_pages(sb, sinode, vma, prop);

    /* Upon successful map, set the ring and index offset*/
    if (ret == 0)
    {
        set_bit(ino, tgroup->map_ring_kaddr);
        if (index_offset)
            *(index_offset) = sinode->index_offset;
    }

    return ret;
}

long sufs_mmap_file(unsigned long arg)
{
    long ret = 0;
    struct sufs_ioctl_map_entry entry;

    if (copy_from_user(&entry, (void*) arg,
            sizeof(struct sufs_ioctl_map_entry)))
        return -EFAULT;

    ret = sufs_do_mmap_file(&sufs_sb, entry.inode, entry.perm,
            &entry.index_offset);

#if 0
    printk("mmap ret is %ld\n", ret);
#endif

    if (ret == 0)
    {
        if (copy_to_user((void *) arg, &entry,
                sizeof(struct sufs_ioctl_map_entry)))
            return -EFAULT;
    }

    return ret;
}

static unsigned long sufs_unmap_file_pages(struct sufs_super_block * sb,
        struct sufs_shadow_inode *sinode, struct vm_area_struct *vma)
{
    struct sufs_fidx_entry *idx = NULL;

    unsigned long offset = 0;

    if (sinode->index_offset == 0)
        return 0;

    idx = (struct sufs_fidx_entry*)
                    sufs_kfs_offset_to_virt_addr(sinode->index_offset);

    /* remove the file page mapping */
    offset = sufs_kfs_virt_addr_to_offset((unsigned long) idx);
    zap_vma_ptes(vma, SUFS_MOUNT_ADDR + offset, PAGE_SIZE);

    while (idx->offset != 0)
    {
        if (likely(sufs_is_norm_fidex(idx)))
        {
            offset = idx->offset;

            /* remove the normal page mapping */
            zap_vma_ptes(vma, SUFS_MOUNT_ADDR + offset, SUFS_FILE_BLOCK_SIZE);

            idx++;
        }
        else
        {
            idx = (struct sufs_fidx_entry*)
                    sufs_kfs_offset_to_virt_addr(idx->offset);

            offset = sufs_kfs_virt_addr_to_offset((unsigned long) idx);

            /* remove the file page mapping */
            zap_vma_ptes(vma, SUFS_MOUNT_ADDR + offset, PAGE_SIZE);
        }
    }

    return 0;
}


/* Do not need to care delete at this stage */
static void sufs_kfs_dir_update_sinode_one_file_block(unsigned long offset)
{
    struct sufs_dir_entry *dir = (struct sufs_dir_entry *)
            sufs_kfs_offset_to_virt_addr(offset);
#if 0
    printk("addr is %lx, offset is %lx, name_len is %d\n", (unsigned long) dir,
            sufs_kfs_virt_addr_to_offset((unsigned long) dir), dir->name_len);
#endif

    while (dir->name_len != 0)
    {
        unsigned short rec_len = dir->rec_len;

        if (dir->ino_num != SUFS_INODE_TOMBSTONE)
        {
            sufs_kfs_set_inode(dir->ino_num, dir->inode.file_type,
                    dir->inode.mode, dir->inode.uid, dir->inode.gid, dir->inode.offset);
#if 0
            printk("Commit: dir->ino_num: %d, dir->inode.file_type: %d, "
                    "dir->inode.mode: %d, dir->inode.uid: %d, "
                    "dir->inode.gid: %d, dir->inode.offset: %lx, dir->name_len: %d\n",
                    dir->ino_num, dir->inode.file_type, dir->inode.mode,
                    dir->inode.uid, dir->inode.gid, dir->inode.offset, dir->name_len);

            printk("addr is %lx, offset is %lx\n", (unsigned long) dir,
                    sufs_kfs_virt_addr_to_offset((unsigned long) dir));

/*            printk(KERN_EMERG "Commit: dir->ino_num: %d, dir->inode.file_type: %d, "
                    "dir->inode.mode: %d, dir->inode.uid: %d, "
                    "dir->inode.gid: %d, dir->inode.offset: %lx\n",
                    dir->ino_num, dir->inode.file_type, dir->inode.mode,
                    dir->inode.uid, dir->inode.gid, dir->inode.offset); */
#endif
        }

        dir = (struct sufs_dir_entry *)
                ((unsigned long) dir + rec_len);

        if (SUFS_KFS_FILE_BLOCK_OFFSET(dir) == 0)
            break;
    }

    return;
}

static void sufs_kfs_dir_update_sinode(struct sufs_shadow_inode * sinode)
{
    struct sufs_fidx_entry *idx = NULL;

    if (sinode->index_offset == 0)
        return;

    idx = (struct sufs_fidx_entry *)
            sufs_kfs_offset_to_virt_addr(sinode->index_offset);

    while (idx->offset != 0)
    {
        if (likely(sufs_is_norm_fidex(idx)))
        {
            sufs_kfs_dir_update_sinode_one_file_block(idx->offset);
            idx++;
        }
        else
        {
            idx = (struct sufs_fidx_entry*) sufs_kfs_offset_to_virt_addr(
                    idx->offset);
        }
    }
}

long sufs_do_unmap_file(struct sufs_super_block * sb, int ino)
{
    struct sufs_tgroup *tgroup = NULL;

    struct vm_area_struct *vma = NULL;

    struct sufs_shadow_inode *sinode = NULL;

    struct sufs_kfs_lease * lease = NULL;

    long ret = 0;

    int tgid = 0;

    tgid = sufs_kfs_pid_to_tgid(current->tgid, 0);

    tgroup = sufs_kfs_tgid_to_tgroup(tgid);

    if (tgroup == NULL)
    {
        printk("Cannot find the tgroup with pid :%d\n", current->tgid);
        return -ENODEV;
    }

    vma = tgroup->mount_vma;

    if (vma == NULL)
    {
        printk("Cannot find the mapped vma\n");
        return -ENODEV;
    }

    sinode = sufs_find_sinode(ino);

    if (sinode == NULL)
    {
        printk("Cannot find sinode with ino %d\n", ino);
        return -EINVAL;
    }

    lease = &(sinode->lease);

    if (lease == NULL)
    {
        printk("sinode with empty lease %d\n", ino);
        return -EINVAL;
    }

    if ((ret = sufs_kfs_release_lease(ino, lease, tgid)) < 0)
    {
        printk("releasing lease error with ino: %d\n", ino);
        return ret;
    }


    if ( (ret = sufs_unmap_file_pages(sb, sinode, vma) < 0))
    {
        printk("unmapping file pages error with ino: %d\n", ino);
        return ret;
    }

    clear_bit(ino, tgroup->map_ring_kaddr);

    /* TODO: This will be removed once we have the consistency check done */
    if (sinode->file_type == SUFS_FILE_TYPE_DIR)
    {
#if 0
        printk("Update inode: %d\n", ino);
#endif
        sufs_kfs_dir_update_sinode(sinode);
    }

    return ret;
}

long sufs_unmap_file(unsigned long arg)
{
    struct sufs_ioctl_map_entry entry;

    if (copy_from_user(&entry, (void*) arg,
            sizeof(struct sufs_ioctl_map_entry)))
        return -EFAULT;

    return sufs_do_unmap_file(&sufs_sb, entry.inode);
}

static int sufs_can_chown(void)
{
    const struct cred * cred = NULL;

    cred = current_cred();

    /* Simplified, only root can chown */
    return (cred->uid.val == 0);
}

static int sufs_do_chown(int ino, int owner, int group,
        unsigned long inode_offset)
{
    struct sufs_shadow_inode *sinode = NULL;
    struct sufs_inode * inode = NULL;

    if (!sufs_can_chown())
        return -EPERM;

    sinode = sufs_find_sinode(ino);

    if (sinode == NULL)
    {
        printk("Cannot find sinode with ino %d\n", ino);
        return -EINVAL;
    }

    /* TODO: Need to validate this is a valid inode */
    inode = (struct sufs_inode * ) sufs_kfs_offset_to_virt_addr(inode_offset);

    if (owner > 0)
    {
        sinode->uid = owner;

        if (inode)
            inode->uid = owner;
    }

    if (group > 0)
    {
        sinode->gid = group;

        if (inode)
            inode->gid = owner;
    }

    return 0;
}

long sufs_chown(unsigned long arg)
{
    struct sufs_ioctl_chown_entry entry;

    if (copy_from_user(&entry, (void*) arg,
            sizeof(struct sufs_ioctl_chown_entry)))
        return -EFAULT;

    return sufs_do_chown(entry.inode, entry.owner, entry.group,
            entry.inode_offset);
}

static int sufs_can_chmod(struct sufs_shadow_inode * sinode)
{
    const struct cred * cred = NULL;

    cred = current_cred();

    /*
     * Simplified:
     * only root can chmod or
     * the owner of the file
     */
    return (cred->uid.val == 0 || sinode->uid == cred->uid.val);
}


static int sufs_do_chmod(int ino, int mode, unsigned long inode_offset)
{
    struct sufs_shadow_inode *sinode = NULL;
    struct sufs_inode * inode = NULL;

    sinode = sufs_find_sinode(ino);

    if (sinode == NULL)
    {
        printk("Cannot find sinode with ino %d\n", ino);
        return -EINVAL;
    }

    if (!sufs_can_chmod(sinode))
        return -EPERM;

    /* TODO: Need to validate that this is a valid inode address */
    inode = (struct sufs_inode * ) sufs_kfs_offset_to_virt_addr(inode_offset);

    sinode->mode = mode;

    if (inode)
        inode->mode = mode;

    return 0;
}


long sufs_chmod(unsigned long arg)
{
    struct sufs_ioctl_chmod_entry entry;

    if (copy_from_user(&entry, (void*) arg,
            sizeof(struct sufs_ioctl_chmod_entry)))
        return -EFAULT;

    return sufs_do_chmod(entry.inode, entry.mode, entry.inode_offset);
}

