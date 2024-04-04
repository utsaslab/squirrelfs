#ifndef SUFS_KFS_MMAP_H_
#define SUFS_KFS_MMAP_H_

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm-generic/io.h>

#include "../include/kfs_config.h"
#include "super.h"
#include "balloc.h"

/* offset in the file system to the kernel virtual address */
static inline unsigned long sufs_kfs_offset_to_virt_addr(unsigned long offset)
{
    return (sufs_sb.start_virt_addr + offset);
}

static inline unsigned long sufs_kfs_virt_addr_to_offset(unsigned long  virt)
{
    return (virt - sufs_sb.start_virt_addr);
}

/* offset in the file system to the kernel virtual address */
static inline unsigned long sufs_kfs_offset_to_pfn(unsigned long offset)
{
    return virt_to_phys((void *) sufs_kfs_offset_to_virt_addr(offset)) >> PAGE_SHIFT;
}

static inline unsigned long sufs_kfs_block_to_pfn(unsigned long block)
{
    return virt_to_phys((void *) sufs_kfs_block_to_virt_addr(block)) >> PAGE_SHIFT;
}


int sufs_kfs_mmap(struct file *filp, struct vm_area_struct *vma);

void sufs_map_pages(struct vm_area_struct * vma,
        unsigned long vaddr, unsigned long pfn, pgprot_t prop, long count);

long sufs_mmap_file(unsigned long arg);

long sufs_do_unmap_file(struct sufs_super_block * sb, int ino);

long sufs_unmap_file(unsigned long arg);

long sufs_chown(unsigned long arg);

long sufs_chmod(unsigned long arg);


#endif /* SUFS_KFS_MMAP_H_ */
