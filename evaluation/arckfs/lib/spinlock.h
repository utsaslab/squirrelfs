#ifndef SUFS_LIB_SPINLOCK_H_
#define SUFS_LIB_SPINLOCK_H_

void sufs_spin_lock(volatile int * lock);

void sufs_spin_unlock(volatile int * lock);

void sufs_spin_init(volatile int * lock);

int sufs_spin_trylock(volatile int * lock);

#endif
