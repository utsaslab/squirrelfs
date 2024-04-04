#include <stdio.h>
#include <stdlib.h>

#include "../include/libfs_config.h"
#include "../include/common_inode.h"
#include "../include/ring_buffer.h"
#include "delegation.h"
#include "stat.h"
#include "random.h"
#include "balloc.h"
#include "simple_ring_buffer.h"
#include "tls.h"

static int sufs_libfs_g_cpt_cnt = 0;


struct sufs_notifyer * sufs_libfs_get_completed_cnt(int tls_index)
{
    unsigned long offset = 0;
    int cpt_idx = 0;
    struct sufs_notifyer * cpt_cnt = NULL;

    cpt_idx = __sync_fetch_and_add(&sufs_libfs_g_cpt_cnt, 1);

    if (cpt_idx >= SUFS_MAX_THREADS)
    {
        fprintf(stderr, "Too many threads!\n");
        abort();
    }

    offset = (cpt_idx * SUFS_ODIN_ONE_CNT_RING_SIZE);

    cpt_cnt = (struct sufs_notifyer *) (SUFS_ODIN_CNT_RING_ADDR + offset);

    sufs_libfs_tls_data[tls_index].cpt_idx = cpt_idx;
    sufs_libfs_tls_data[tls_index].cpt_cnt = cpt_cnt;

    return cpt_cnt;
}

static inline int sufs_libfs_choose_rings(struct sufs_libfs_super_block *sb)
{
    return sufs_libfs_xor_random() % sb->dele_ring_per_node;
}

unsigned int sufs_libfs_do_read_delegation(struct sufs_libfs_super_block *sb,
                     unsigned long uaddr, unsigned long offset,
                     unsigned long bytes,
                     int zero, long * issued_cnt,
                     int completed_idx, int completed_level)
{
    int pm_node = 0;
    unsigned int ret = 0;
    struct sufs_delegation_request request;
    unsigned long i = 0, uaddr_end = 0, block = 0;
    int thread = 0;

#if 0
    SUFS_LIBFS_DEFINE_TIMING_VAR(prefault_time);
    SUFS_LIBFS_DEFINE_TIMING_VAR(send_request_time);
#endif

    /*
     * access the user address while still at the process's address space
     * to let the kernel handles various situations: e.g., page not mapped,
     * page swapped out.
     *
     * Surprisingly, the overhead of this part is significant. However,
     * currently it looks to me no point to optimize this part; The bottleneck
     * is in the delegation thread, not the main thread.
     */

#if 0
    SUFS_LIBFS_START_TIMING(pre_fault_r_t, prefault_time);
#endif
    uaddr_end = PAGE_ROUND_UP(uaddr + bytes - 1);
    for (i = uaddr; i < uaddr_end; i += SUFS_PAGE_SIZE)
    {
        unsigned long target_addr = i;

        /*
         * Do not access an address that is out of the buffer provided by the
         * user
         */

        if (i > uaddr + bytes - 1)
            target_addr = uaddr + bytes - 1;
#if 0
        printf("uaddr: %lx, bytes: %ld, target_addr: %lx\n", uaddr, bytes,
                target_addr);
#endif

        *((char *) target_addr) = 0;

    }
#if 0
    SUFS_LIBFS_END_TIMING(pre_fault_r_t, prefault_time);
#endif

    /*
     * We have ensured that [kaddr, kaddr + bytes - 1) falls in the same
     * kernel page.
     */

    /* which socket to delegate */

    block = sufs_libfs_offset_to_block(offset);
    pm_node = sufs_libfs_block_to_pm_node(sb, block);

    /* inc issued cnt */
    issued_cnt[pm_node]++;

    request.type = SUFS_DELE_REQUEST_READ;
    request.uaddr = uaddr;
    request.offset = offset;
    request.bytes = bytes;
    request.zero = zero;
    request.notify_idx = completed_idx;
    request.level = completed_level;

#if 0
    SUFS_LIBFS_START_TIMING(send_request_r_t, send_request_time);
#endif
    do
    {
        thread = sufs_libfs_choose_rings(sb);
        ret = sufs_libfs_sr_send_request(sufs_libfs_ring_buffers[pm_node][thread],
                &request);
    } while (ret == -SUFS_RBUFFER_AGAIN);

#if 0
    SUFS_LIBFS_END_TIMING(send_request_r_t, send_request_time);
#endif

    return ret;
}


unsigned int sufs_libfs_do_write_delegation(struct sufs_libfs_super_block * sb,
                      unsigned long uaddr, unsigned long offset,
                      unsigned long bytes,
                      int zero, int flush_cache, int sfence,
                      long * issued_cnt,
                      int completed_idx, int completed_level, int index)
{
    int pm_node = 0;
    struct sufs_delegation_request request;
    int ret = 0;
    unsigned long i = 0, uaddr_end = 0, block = 0;
    int thread = 0;

#if 0
    SUFS_LIBFS_DEFINE_TIMING_VAR(prefault_time);
    SUFS_LIBFS_DEFINE_TIMING_VAR(send_request_time);
#endif

    /* TODO: Check the validity of the user-level address */

    /*
   * access the user address while still at the process's address space
   * to let the kernel handles various situations: e.g., page not mapped,
   * page swapped out.
   *
   * Surprisingly, the overhead of this part is significant. However,
   * currently it looks to me no point to optimize this part; The bottleneck
   * is in the delegation thread, not the main thread.
   */
    if (!zero)
    {
#if 0
        SUFS_LIBFS_START_TIMING(pre_fault_w_t, prefault_time);
#endif
        uaddr_end = PAGE_ROUND_UP(uaddr + bytes - 1);

        for (i = uaddr; i < uaddr_end; i += SUFS_PAGE_SIZE)
        {
            unsigned long target_addr = i;

            /*
             * Do not access an address that is out of the buffer provided by
             * the user
             */

            if (i > uaddr + bytes - 1)
                target_addr = uaddr + bytes - 1;

#if 0
            printf("uaddr: %lx, bytes: %ld, target_addr: %lx\n", uaddr, bytes,
                    target_addr);
#endif

            sufs_libfs_tls_data[index].pad[0] = * (char *) target_addr;
        }

#if 0
        SUFS_LIBFS_END_TIMING(pre_fault_w_t, prefault_time);
#endif
    }

    /*
     * We have ensured that [kaddr, kaddr + bytes - 1) falls in the same
     * kernel page.
     */

    /* which socket to delegate */
    block = sufs_libfs_offset_to_block(offset);
    pm_node = sufs_libfs_block_to_pm_node(sb, block);

    /* inc issued cnt */
    issued_cnt[pm_node]++;

    request.type = SUFS_DELE_REQUEST_WRITE;
    request.uaddr = uaddr;
    request.offset = offset;
    request.bytes = bytes;
    request.zero = zero;
    request.flush_cache = flush_cache;
    request.notify_idx = completed_idx;
    request.level = completed_level;

#if 0
    printf("LibFS pm_node: %d, thread: %d, ring_address: %lx\n",
            pm_node, thread, (unsigned long)
            sufs_libfs_ring_buffers[pm_node][thread]);

    printf("LibFS send request request.type: %d, request.uaddr: %lx, "
            "request.offset: %lx, request.bytes: %ld, request.zero: %d, "
            "request.flush_cache: %d, request.notify_idx: %d\n",
            request.type, request.uaddr, request.offset, request.bytes,
            request.zero, request.flush_cache, request.notify_idx);
#endif

#if 0
    SUFS_LIBFS_START_TIMING(send_request_w_t, send_request_time);
#endif
    do
    {
        thread = sufs_libfs_choose_rings(sb);
        ret = sufs_libfs_sr_send_request(sufs_libfs_ring_buffers[pm_node][thread],
                &request);
    } while (ret == -SUFS_RBUFFER_AGAIN);

#if 0
    SUFS_LIBFS_END_TIMING(send_request_w_t, send_request_time);
#endif

    return ret;
}

void sufs_libfs_complete_delegation(struct sufs_libfs_super_block *sb,
        long * issued_cnt, struct sufs_notifyer * completed_cnt)
{
    int i = 0;

#if 0
    for (i = 0; i < sb->pm_nodes; i++)
    {
        printf("i: %d, completed_cnt_addr: %lx\n", i,
                (unsigned long) &(completed_cnt[i].cnt));
    }
#endif

    for (i = 0; i < sb->pm_nodes; i++)
    {
        long issued = issued_cnt[i];

        /* TODO: Some kind of backoff is needed here? */
        while (issued != completed_cnt[i].cnt);
    }
}
