#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>

#include "../include/kfs_config.h"
#include "../include/ring_buffer.h"
#include "../lib/spinlock.h"

#include "simple_ring_buffer.h"
#include "agent.h"

struct sufs_ring_buffer * sufs_kfs_sr_create(int index, int entry_size)
{
    int size = SUFS_ODIN_ONE_RING_SIZE, buffer_size = 0;
    struct sufs_ring_buffer *ret;

    ret = (struct sufs_ring_buffer *) (sufs_kfs_buffer_ring_kaddr[index]);

    ret->kfs_requests = (struct sufs_ring_buffer_entry *)
            (((unsigned long) ret) + sizeof(struct sufs_ring_buffer));

    ret->comsumer_idx = 0;
    ret->producer_idx = 0;
    ret->entry_size = entry_size;

    /* See comments in simple_ring_buffer.h */
    buffer_size = size - sizeof(struct sufs_ring_buffer);
    ret->num_of_entry = buffer_size / (sizeof(struct sufs_ring_buffer_entry));

    sufs_spin_init(&ret->spinlock);

    return ret;
}

int sufs_kfs_sr_send_request(struct sufs_ring_buffer *ring, void *from)
{
    unsigned long irq = 0;
    int ret = 0;

    local_irq_save(irq);
    sufs_spin_lock(&ring->spinlock);

    if (ring->kfs_requests[ring->producer_idx].valid)
    {
        ret = -SUFS_RBUFFER_AGAIN;
        goto out;
    }

    memcpy(&(ring->kfs_requests[ring->producer_idx].request), from,
           ring->entry_size);

    ring->kfs_requests[ring->producer_idx].valid = 1;

    ring->producer_idx = (ring->producer_idx + 1) % (ring->num_of_entry);

out:
    sufs_spin_unlock(&ring->spinlock);
    local_irq_restore(irq);
    return ret;
}



int sufs_kfs_sr_receive_request(struct sufs_ring_buffer *ring, void *to)
{
    int ret = 0;

    /* Spin to wait for a new entry */
#if 0
    local_irq_save(irq);
    sufs_spin_lock(&ring->spinlock);
#endif

    if (!ring->kfs_requests[ring->comsumer_idx].valid)
    {
        ret = -SUFS_RBUFFER_AGAIN;
        goto out;
    }

    memcpy(to, &(ring->kfs_requests[ring->comsumer_idx].request),
           ring->entry_size);

    ring->kfs_requests[ring->comsumer_idx].valid = 0;

    ring->comsumer_idx = (ring->comsumer_idx + 1) % ring->num_of_entry;

out:
#if 0
    sufs_spin_unlock(&ring->spinlock);
    local_irq_restore(irq);
#endif
    return ret;
}
