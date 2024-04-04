#ifndef SUFS_LIBFS_SIMPLE_RING_BUFFER_H_
#define SUFS_LIBFS_SIMPLE_RING_BUFFER_H_

#include "../../include/libfs_config.h"
#include "../../include/ring_buffer.h"
#include "super.h"

extern struct sufs_ring_buffer * sufs_libfs_ring_buffers[SUFS_PM_MAX_INS][SUFS_MAX_CPU / SUFS_PM_MAX_INS];

int sufs_libfs_sr_send_request(struct sufs_ring_buffer *ring, void *from);

void sufs_libfs_ring_buffer_connect(struct sufs_libfs_super_block * sb);

#endif
