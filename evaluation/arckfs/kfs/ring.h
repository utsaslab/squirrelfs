#ifndef SUFS_KFS_RING_H_
#define SUFS_KFS_RING_H_

#include "../include/kfs_config.h"
#include "tgroup.h"

int sufs_kfs_create_ring(struct sufs_tgroup * tgroup);

void sufs_kfs_delete_ring(struct sufs_tgroup * tgroup);

int sufs_kfs_allocate_pages(unsigned long size, int node,
        unsigned long ** kaddr, struct page ** kpage);

int sufs_kfs_mmap_pages_to_user(unsigned long addr, unsigned long size,
        struct vm_area_struct * vma, int user_writable, struct page * pg);

#endif
