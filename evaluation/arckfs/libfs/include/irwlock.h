/* wrapper layer of the reader writer lock */

#ifndef SUFS_LIBFS_RWLOCK_H_
#define SUFS_LIBFS_RWLOCK_H_

#include <pthread.h>
#include "../../include/libfs_config.h"

#include "rwlock_bravo.h"

#define sufs_libfs_inode_rw_lock(m) (&(m->data.file_data.rw_lock))


#if SUFS_INODE_RW_LOCK == SUFS_INODE_RW_LOCK_SPIN
#define sufs_libfs_inode_read_lock(i) (pthread_spin_lock(sufs_libfs_inode_rw_lock(i)))
#elif SUFS_INODE_RW_LOCK == SUFS_INODE_RW_LOCK_BRAVO
#define sufs_libfs_inode_read_lock(i) (sufs_libfs_bravo_read_lock(sufs_libfs_inode_rw_lock(i)))
#endif


#if SUFS_INODE_RW_LOCK == SUFS_INODE_RW_LOCK_SPIN
#define sufs_libfs_inode_read_unlock(i) (pthread_spin_unlock(sufs_libfs_inode_rw_lock(i)))
#elif SUFS_INODE_RW_LOCK == SUFS_INODE_RW_LOCK_BRAVO
#define sufs_libfs_inode_read_unlock(i) (sufs_libfs_bravo_read_unlock(sufs_libfs_inode_rw_lock(i)))
#endif

#if SUFS_INODE_RW_LOCK == SUFS_INODE_RW_LOCK_SPIN
#define sufs_libfs_inode_write_lock(i) (pthread_spin_lock(sufs_libfs_inode_rw_lock(i)))
#elif SUFS_INODE_RW_LOCK == SUFS_INODE_RW_LOCK_BRAVO
#define sufs_libfs_inode_write_lock(i) (sufs_libfs_bravo_write_lock(sufs_libfs_inode_rw_lock(i)))
#endif

#if SUFS_INODE_RW_LOCK == SUFS_INODE_RW_LOCK_SPIN
#define sufs_libfs_inode_write_unlock(i) (pthread_spin_unlock(sufs_libfs_inode_rw_lock(i)))
#elif SUFS_INODE_RW_LOCK == SUFS_INODE_RW_LOCK_BRAVO
#define sufs_libfs_inode_write_unlock(i) (sufs_libfs_bravo_write_unlock(sufs_libfs_inode_rw_lock(i)))
#endif


#if SUFS_INODE_RW_LOCK == SUFS_INODE_RW_LOCK_SPIN
#define sufs_libfs_inode_rwlock_init(i) (pthread_spin_init(sufs_libfs_inode_rw_lock(i), PTHREAD_PROCESS_SHARED))
#elif SUFS_INODE_RW_LOCK == SUFS_INODE_RW_LOCK_BRAVO
#define sufs_libfs_inode_rwlock_init(i) (sufs_libfs_bravo_rwlock_init(sufs_libfs_inode_rw_lock(i)))
#endif

#if SUFS_INODE_RW_LOCK == SUFS_INODE_RW_LOCK_SPIN
#define sufs_libfs_inode_rwlock_destroy(i) (pthread_spin_destroy(sufs_libfs_inode_rw_lock(i)))
#elif SUFS_INODE_RW_LOCK == SUFS_INODE_RW_LOCK_BRAVO
#define sufs_libfs_inode_rwlock_destroy(i) (sufs_libfs_bravo_rwlock_destroy(sufs_libfs_inode_rw_lock(i)))
#endif




#endif
