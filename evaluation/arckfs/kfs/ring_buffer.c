#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "../include/kfs_config.h"
#include "ring_buffer.h"
#include "simple_ring_buffer.h"
#include "agent.h"
#include "ring.h"

struct sufs_ring_buffer * sufs_kfs_ring_buffer[SUFS_PM_MAX_INS][SUFS_MAX_CPU / SUFS_PM_MAX_INS];

void sufs_kfs_init_ring_buffers(int nodes)
{
    int i = 0, j = 0;

    int num_of_rings_per_socket = sufs_sb.dele_ring_per_node;

    /* allocate the space for counter */
    for (i = 0; i < SUFS_MAX_THREADS; i++)
    {
        /* socket = i / sufs_sb.cpus_per_socket; */

        sufs_kfs_allocate_pages(SUFS_ODIN_ONE_CNT_RING_SIZE, NUMA_NO_NODE,
                (unsigned long **) &(sufs_kfs_counter_addr[i]),
                &(sufs_kfs_counter_pg[i]));
    }

    /* allocate the space for the ring buffer */
    for (i = 0; i < nodes; i++)
    {
        for (j = 0; j < num_of_rings_per_socket; j++)
        {
            int index = i * num_of_rings_per_socket + j;

            sufs_kfs_allocate_pages(SUFS_ODIN_ONE_RING_SIZE, i,
                    (unsigned long **) &(sufs_kfs_buffer_ring_kaddr[index]),
                    &(sufs_kfs_buffer_ring_pg[index]));

            sufs_kfs_ring_buffer[i][j] = sufs_kfs_sr_create(index,
                            sizeof(struct sufs_delegation_request));

#if 0
            printk("node: %d, cpu: %d, kaddr: %lx\n",
                    i, j, (unsigned long) sufs_kfs_ring_buffer[i][j]);
#endif
        }
    }
}

void sufs_kfs_fini_ring_buffers(int nodes)
{
    int i = 0, j = 0;

    int num_of_rings_per_socket = sufs_sb.dele_ring_per_node;

    for (i = 0; i < SUFS_MAX_THREADS; i++)
    {
        __free_pages(sufs_kfs_counter_pg[i],
                get_order(SUFS_ODIN_ONE_CNT_RING_SIZE));
    }

    /* allocate the space for the ring buffer */
    for (i = 0; i < nodes; i++)
    {
        for (j = 0; j < num_of_rings_per_socket; j++)
        {
            int index = i * num_of_rings_per_socket + j;

            __free_pages(sufs_kfs_buffer_ring_pg[index],
                    get_order(SUFS_ODIN_ONE_RING_SIZE));
        }
    }
}
