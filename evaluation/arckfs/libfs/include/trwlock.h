#ifndef SUFS_LIBFS_TRWLOCK_H_
#define SUFS_LIBFS_TRWLOCK_H_

#include "../../include/libfs_config.h"
#include "compiler.h"
#include "atomic_util.h"

typedef union sufs_libfs_rwticket sufs_libfs_rwticket;

union sufs_libfs_rwticket
{
    unsigned int u;
    unsigned short us;
    __extension__ struct
    {
        unsigned char write;
        unsigned char read;
        unsigned char users;
    } s;
};

static inline void sufs_libfs_rwticket_wrlock(sufs_libfs_rwticket *l)
{
    unsigned int me = sufs_libfs_atomic_xadd(&l->u, (1<<16));
    unsigned char val = me >> 16;

    while (val != l->s.write)
        sufs_cpu_relax();
}

static inline void sufs_libfs_rwticket_wrunlock(sufs_libfs_rwticket *l)
{
    sufs_libfs_rwticket t = *l;

    sufs_barrier();

    t.s.write++;
    t.s.read++;

    *(unsigned short *) l = t.us;
}

static inline void sufs_libfs_rwticket_rdlock(sufs_libfs_rwticket *l)
{
    unsigned int me = sufs_libfs_atomic_xadd(&l->u, (1<<16));
    unsigned char val = me >> 16;

    while (val != l->s.read)
    {
        sufs_cpu_relax();
    }

    l->s.read++;
}

static inline void sufs_libfs_rwticket_rdunlock(sufs_libfs_rwticket *l)
{
    sufs_libfs_atomic_inc(&l->s.write);
}

#endif
