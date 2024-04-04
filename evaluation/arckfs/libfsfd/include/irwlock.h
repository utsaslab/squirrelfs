/* wrapper layer of the reader writer lock */

#ifndef SUFS_LIBFS_RWLOCK_H_
#define SUFS_LIBFS_RWLOCK_H_

#include <pthread.h>
#include "../../include/libfs_config.h"

#include "rwlock_bravo.h"

#define sufs_libfs_inode_rw_lock(m) (&(m->data.file_data.rw_lock))


#define sufs_libfs_inode_read_lock(i) (pthread_spin_lock(sufs_libfs_inode_rw_lock(i)))
#define sufs_libfs_inode_read_unlock(i) (pthread_spin_unlock(sufs_libfs_inode_rw_lock(i)))
#define sufs_libfs_inode_write_lock(i) (pthread_spin_lock(sufs_libfs_inode_rw_lock(i)))
#define sufs_libfs_inode_write_unlock(i) (pthread_spin_unlock(sufs_libfs_inode_rw_lock(i)))
#define sufs_libfs_inode_rwlock_init(i) (pthread_spin_init(sufs_libfs_inode_rw_lock(i), PTHREAD_PROCESS_SHARED))
#define sufs_libfs_inode_rwlock_destroy(i) (pthread_spin_destroy(sufs_libfs_inode_rw_lock(i)))




#endif
