#ifndef SUFS_LIBFS_DELEGATION_H_
#define SUFS_LIBFS_DELEGATION_H_

#include "../../include/libfs_config.h"

#include "super.h"

struct sufs_notifyer * sufs_libfs_get_completed_cnt(int tls_index);

unsigned int sufs_libfs_do_read_delegation(struct sufs_libfs_super_block *sb,
                     unsigned long uaddr, unsigned long offset,
                     unsigned long bytes,
                     int zero, long * issued_cnt,
                     int completed_idx, int completed_level);

unsigned int sufs_libfs_do_write_delegation(struct sufs_libfs_super_block * sb,
                      unsigned long uaddr, unsigned long offset,
                      unsigned long bytes,
                      int zero, int flush_cache, int sfence,
                      long * issued_cnt,
                      int completed_idx, int completed_level, int index);

void sufs_libfs_complete_delegation(struct sufs_libfs_super_block *sb,
        long * issued_cnt, struct sufs_notifyer * completed_cnt);


#endif /* __DELEGATION_H_ */
