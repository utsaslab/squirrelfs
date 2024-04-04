#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/memory.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <asm-generic/io.h>

#include "../include/kfs_config.h"
#include "../include/ring_buffer.h"
#include "ring.h"
#include "super.h"
#include "agent.h"

/* Total ring size */
#define SUFS_RING_SIZE (SUFS_LEASE_RING_SIZE + SUFS_MAPPED_RING_SIZE + \
                        SUFS_ODIN_CNT_RING_TOT_SIZE + SUFS_ODIN_RING_SIZE)

int sufs_kfs_allocate_pages(unsigned long size, int node,
        unsigned long ** kaddr, struct page ** kpage)
{
    int order = 0;
    unsigned long base_pfn = 0;
    struct page * pg = NULL;

    order = get_order(size);

    pg = alloc_pages_node(node, GFP_KERNEL | __GFP_ZERO, order);

    if (!pg)
    {
        printk("alloc failed with size: %ld\n", size);
        return -ENOMEM;
    }


    base_pfn = page_to_pfn(pg);

    if (kaddr)
    {
        (*kaddr) = page_to_virt(pg);
    }

    if (kpage)
    {
        (*kpage) = pg;
    }

    return 0;
}

int sufs_kfs_mmap_pages_to_user(unsigned long addr, unsigned long size,
        struct vm_area_struct * vma, int user_writable, struct page * pg)
{
    unsigned long i = 0, base_pfn = 0;
    pgprot_t prop;
    vm_fault_t rc;


    if (user_writable)
    {
        prop = vm_get_page_prot(VM_READ|VM_WRITE|VM_SHARED);
    }
    else
    {
        prop = vm_get_page_prot(VM_READ|VM_SHARED);
    }

    base_pfn = page_to_pfn(pg);

    for (i = 0; i < size / PAGE_SIZE; i++)
    {
        if ((rc = vmf_insert_pfn_prot(vma, addr + i * PAGE_SIZE,
                base_pfn + i, prop)) != VM_FAULT_NOPAGE)
        {
            printk("insert pfn root failed with rc: %x\n", rc);
            return -ENOENT;
        }
    }

    return 0;
}

static int sufs_kfs_mmap_ring(unsigned long addr, unsigned long size,
        struct vm_area_struct * vma, int user_writable,
        unsigned long ** kaddr, struct page ** kpage, int node)
{
    int ret = 0;

    struct page * page = NULL;

    if ((ret = sufs_kfs_allocate_pages(size, node, kaddr, &page)) != 0)
        return ret;

    if (kpage)
    {
        (*kpage) = page;
    }

    if ((ret = sufs_kfs_mmap_pages_to_user(addr, size, vma, user_writable, page)) != 0)
        return ret;

    return 0;

}

/*
 * Create ring buffers for the trust group at the specified address with the
 * specified size
 */
int sufs_kfs_create_ring(struct sufs_tgroup * tgroup)
{
    int ret = 0, i = 0, j = 0;
    struct vm_area_struct * vma = NULL;

    ret = vm_mmap(NULL, SUFS_RING_ADDR, SUFS_RING_SIZE,
            PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, 0);

    if (ret < 0)
        return ret;

    vma = find_vma(current->mm, SUFS_RING_ADDR);

    /*  Makes vmf_insert_pfn_prot happy */
    /*  TODO: validate whether VM_PFNMAP has any side effect or not */
    vm_flags_set(vma, VM_PFNMAP);

    ret = sufs_kfs_mmap_ring(SUFS_LEASE_RING_ADDR,
            SUFS_LEASE_RING_SIZE, vma, 1,
            &(tgroup->lease_ring_kaddr), &(tgroup->lease_ring_pg), NUMA_NO_NODE);

    if (ret < 0)
        goto err;

    ret = sufs_kfs_mmap_ring(SUFS_MAPPED_RING_ADDR,
            SUFS_MAPPED_RING_SIZE, vma, 1,
            &(tgroup->map_ring_kaddr), &(tgroup->map_ring_pg), NUMA_NO_NODE);

    if (ret < 0)
        goto err;

    for (i = 0; i < SUFS_MAX_THREADS; i++)
    {
        ret = sufs_kfs_mmap_pages_to_user(
                SUFS_ODIN_CNT_RING_ADDR + i * SUFS_ODIN_ONE_CNT_RING_SIZE,
                SUFS_ODIN_ONE_CNT_RING_SIZE, vma, 1, sufs_kfs_counter_pg[i]);

        if (ret < 0)
            goto err;
    }


    for (i = 0; i < sufs_sb.pm_nodes; i++)
    {
        for (j = 0; j < sufs_sb.dele_ring_per_node; j++)
        {
            int index = i * (sufs_sb.dele_ring_per_node) + j;

            ret = sufs_kfs_mmap_pages_to_user(
                    SUFS_ODIN_RING_ADDR + index * SUFS_ODIN_ONE_RING_SIZE,
                    SUFS_ODIN_ONE_RING_SIZE, vma, 1,
                    sufs_kfs_buffer_ring_pg[index]);

            if (ret < 0)
                goto err;
        }
    }


    return ret;

err:
    printk("vm_mmap failed!\n");
    vm_munmap(SUFS_RING_ADDR, SUFS_RING_SIZE);
    return -ENOMEM;
}

void sufs_kfs_delete_ring(struct sufs_tgroup * tgroup)
{
    __free_pages(tgroup->lease_ring_pg, get_order(SUFS_LEASE_RING_SIZE));
    tgroup->lease_ring_kaddr = 0;
    tgroup->lease_ring_pg = NULL;

    __free_pages(tgroup->map_ring_pg, get_order(SUFS_MAPPED_RING_SIZE));
    tgroup->map_ring_kaddr = 0;
    tgroup->map_ring_pg = NULL;

    vm_munmap(SUFS_RING_ADDR, SUFS_RING_SIZE);
}
