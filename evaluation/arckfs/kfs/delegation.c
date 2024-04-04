#include <linux/percpu.h>
#include <linux/uaccess.h>
#include <linux/random.h>

#include "../include/kfs_config.h"
#include "../include/common_inode.h"
#include "../include/ring_buffer.h"

#include "delegation.h"
#include "simple_ring_buffer.h"
#include "super.h"
#include "stat.h"
#include "balloc.h"
#include "ring_buffer.h"

DEFINE_PER_CPU(u32, sufs_kfs_seed);

static inline unsigned int sufs_kfs_xor_random(void)
{
    unsigned int v;

    v = this_cpu_read(sufs_kfs_seed);

    if (v == 0)
        get_random_bytes(&v, sizeof(unsigned int));

    v ^= v << 6;
    v ^= v >> 21;
    v ^= v << 7;
    this_cpu_write(sufs_kfs_seed, v);

    return v;
}

static inline int sufs_kfs_choose_rings(struct sufs_super_block *sb)
{
    return sufs_kfs_xor_random() % sb->dele_ring_per_node;
}

unsigned int sufs_kfs_do_clear_delegation(struct sufs_super_block * sb,
                      unsigned long offset, unsigned long bytes,
                      int flush_cache, int sfence,
                      long * issued_cnt, struct sufs_notifyer * complete_cnt)
{
    int pm_node = 0;
    struct sufs_delegation_request request;
    int ret = 0;
    unsigned long block = 0;
    int thread = 0;

    SUFS_KFS_DEFINE_TIMING_VAR(prefault_time);
    SUFS_KFS_DEFINE_TIMING_VAR(send_request_time);


    /*
     * We have ensured that [kaddr, kaddr + bytes - 1) falls in the same
     * kernel page.
     */

    /* which socket to delegate */
    block = sufs_kfs_offset_to_block(offset);
    pm_node = sufs_block_to_pm_node(sb, block);

    /* inc issued cnt */
    issued_cnt[pm_node]++;

    request.type = SUFS_DELE_REQUEST_KFS_CLEAR;
    request.uaddr = 0;
    request.offset = offset;
    request.bytes = bytes;
    request.zero = 1;
    request.flush_cache = flush_cache;
    request.kidx_ptr = (unsigned long) (&(complete_cnt[pm_node]));

    SUFS_KFS_START_TIMING(send_request_w_t, send_request_time);
    do
    {
        thread = sufs_kfs_choose_rings(sb);
        ret = sufs_kfs_sr_send_request(sufs_kfs_ring_buffer[pm_node][thread],
                &request);

    } while (ret == -SUFS_RBUFFER_AGAIN);

    SUFS_KFS_END_TIMING(send_request_w_t, send_request_time);

    return ret;
}


void sufs_kfs_complete_delegation(struct sufs_super_block *sb,
        long * issued_cnt, struct sufs_notifyer * completed_cnt)
{
    int i = 0;


    for (i = 0; i < sb->pm_nodes; i++)
    {
        long issued = issued_cnt[i];
        unsigned long cond_cnt = 0;

        /* TODO: Some kind of backoff is needed here? */
        while (issued != completed_cnt[i].cnt)
        {
            cond_cnt++;

            if (cond_cnt >= SUFS_KFS_APP_CHECK_COUNT)
            {
                cond_cnt = 0;
                if (need_resched())
                    cond_resched();
            }
        }
    }
}
