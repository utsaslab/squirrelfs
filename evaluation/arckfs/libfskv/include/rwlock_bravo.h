#ifndef SUFS_LIBFS_RWLOCK_BRAVO_H_
#define SUFS_LIBFS_RWLOCK_BRAVO_H_

#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#include "../../include/libfs_config.h"

struct sufs_libfs_bravo_rwlock_struct
{
    bool rbias;
    unsigned long inhibit_until;
    pthread_rwlock_t underlying;
};

static inline void
sufs_libfs_bravo_rwlock_init(struct sufs_libfs_bravo_rwlock_struct *l)
{
    l->rbias = true;
    l->inhibit_until = 0;

    if (pthread_rwlock_init(&l->underlying, NULL))
    {
        abort();
    }
}

static void inline
sufs_libfs_bravo_rwlock_destroy(struct sufs_libfs_bravo_rwlock_struct *l)
{
    pthread_rwlock_destroy(&l->underlying);
}

static void inline
sufs_libfs_bravo_write_unlock(struct sufs_libfs_bravo_rwlock_struct *l)
{
    pthread_rwlock_unlock(&l->underlying);
}


void sufs_libfs_bravo_read_lock(struct sufs_libfs_bravo_rwlock_struct *l);
void sufs_libfs_bravo_read_unlock(struct sufs_libfs_bravo_rwlock_struct *l);

void sufs_libfs_bravo_write_lock(struct sufs_libfs_bravo_rwlock_struct *l);

#endif /* BRAVO_H */
