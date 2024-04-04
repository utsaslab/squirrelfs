#ifndef SUFS_KFS_RING_BUFFER_H_
#define SUFS_KFS_RING_BUFFER_H_

#include "../include/kfs_config.h"
#include "simple_ring_buffer.h"

extern struct sufs_ring_buffer * sufs_kfs_ring_buffer[SUFS_PM_MAX_INS][SUFS_MAX_CPU / SUFS_PM_MAX_INS];

void sufs_kfs_init_ring_buffers(int nodes);

void sufs_kfs_fini_ring_buffers(int nodes);

#endif
