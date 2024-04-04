#ifndef SUFS_GLOBAL_KFS_CONFIG_H_
#define SUFS_GLOBAL_KFS_CONFIG_H_

#include "config.h"

#define SUFS_KFS_DEF_DELE_THREADS_PER_SOCKET 0

#define SUFS_KFS_MAX_AGENT_PER_SOCKET (SUFS_MAX_CPU / SUFS_PM_MAX_INS)
#define SUFS_KFS_MAX_AGENT (SUFS_MAX_CPU)

/*
 * Copy from OdinFS.
 * Unclear why we need an extra 1
 */
#define SUFS_KFS_AGENT_TASK_MAX_SIZE (SUFS_FILE_BLOCK_PAGE_CNT + 1)


/*
 * Do cond_schuled()/kthread_should_stop() every 100ms when agents are spinning
 * on the ring buffer
 *
 * This assumes that one ring buffer acquire operation takes 100 cycles
 */

#define SUFS_KFS_AGENT_RING_BUFFER_CHECK_COUNT 220000

/*
 * Do cond_schuled()/kthread_should_stop() every 100ms when agents are serving
 * requests. The 3000 value is set with 32KB strip size where memcpy 32KB
 * takes around 70000 cycles. So (2.2*10^9) / 70000 = 3000
 */
#define SUFS_KFS_AGENT_REQUEST_CHECK_COUNT 3000

/*
 * Do cond_schuled() every 100ms when the application thread is waiting for
 * the delegation request to complete.
 */
#define SUFS_KFS_APP_CHECK_COUNT 220000000

#endif
