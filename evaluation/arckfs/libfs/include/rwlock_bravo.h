#ifndef SUFS_LIBFS_RWLOCK_BRAVO_H_
#define SUFS_LIBFS_RWLOCK_BRAVO_H_

#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#include "../../include/libfs_config.h"
#include "trwlock.h"

struct sufs_libfs_bravo_rwlock
{
    bool rbias;
    unsigned long inhibit_until;
    sufs_libfs_rwticket underlying;
};

static inline void
sufs_libfs_bravo_rwlock_init(struct sufs_libfs_bravo_rwlock *l)
{
    l->rbias = true;
    l->inhibit_until = 0;
    l->underlying.u = 0;
}

static void inline
sufs_libfs_bravo_rwlock_destroy(struct sufs_libfs_bravo_rwlock *l)
{

}

static void inline
sufs_libfs_bravo_write_unlock(struct sufs_libfs_bravo_rwlock *l)
{
    sufs_libfs_rwticket_wrunlock(&l->underlying);
}


void sufs_libfs_bravo_read_lock(struct sufs_libfs_bravo_rwlock *l);
void sufs_libfs_bravo_read_unlock(struct sufs_libfs_bravo_rwlock *l);

void sufs_libfs_bravo_write_lock(struct sufs_libfs_bravo_rwlock *l);

#endif /* BRAVO_H */
