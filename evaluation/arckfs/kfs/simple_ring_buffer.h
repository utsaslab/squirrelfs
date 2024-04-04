#ifndef SUFS_KFS_SIMPLE_RING_BUFFER_H_
#define SUFS_KFS_SIMPLE_RING_BUFFER_H_

#include "../include/kfs_config.h"
#include "../include/ring_buffer.h"

#include "tgroup.h"

struct sufs_ring_buffer * sufs_kfs_sr_create(int index, int entry_size);

int sufs_kfs_sr_send_request(struct sufs_ring_buffer *ring, void *from);

int sufs_kfs_sr_receive_request(struct sufs_ring_buffer *ring, void *to);

#endif
