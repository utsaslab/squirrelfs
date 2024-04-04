#ifndef SUFS_KFS_DELEGATION_H_
#define SUFS_KFS_DELEGATION_H_

#include <linux/types.h>

#include "../include/kfs_config.h"
#include "../include/ring_buffer.h"
#include "super.h"

unsigned int sufs_kfs_do_clear_delegation(struct sufs_super_block * sb,
                      unsigned long offset, unsigned long bytes,
                      int flush_cache, int sfence,
                      long * issued_cnt, struct sufs_notifyer * complete_cnt);

void sufs_kfs_complete_delegation(struct sufs_super_block *sb,
        long * issued_cnt, struct sufs_notifyer * completed_cnt);

#endif
