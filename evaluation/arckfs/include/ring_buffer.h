#ifndef SUFS_GLOBAL_RING_BUFFER_H_
#define SUFS_GLOBAL_RING_BUFFER_H_

#include "config.h"

#define SUFS_RBUFFER_AGAIN   1

/*
 * TODO: This needs to be further optimized, we want to minimize the
 * size of copying
 */

/* TODO: verify the correctness of cacheline break */
struct sufs_delegation_request {
    /* read, write */
    int type;

    /*
     * for read requests, memset [uaddr, uaddr + size) with 0.
     * for write request, memset [kaddr, kaddr + size) with 0.
     * Useful for sparse file and delegating memset.
     */

    int zero;

    /* for write request, flush the cache or not. */
    int flush_cache;

    /* for write request, do sfence or not. */
    int sfence;

    /* user address, kernel address, size of the request */
    unsigned long uaddr, offset, bytes;


    int notify_idx;
    int level;

    unsigned long kidx_ptr;

    char pad[8];
};


/* TODO: verify the correctness of cacheline break */
struct sufs_notifyer
{
    volatile int cnt;
    /* cache line break */
    char pad[60];
};



struct sufs_ring_buffer_entry {
    /* This is not perfect, but keep it for the sake of simplicity */
    struct sufs_delegation_request request;
    volatile int valid;
    /* cache line break */
    char pad[60];
};

struct sufs_ring_buffer {
    /* First cache line: read only data */
    int num_of_entry, entry_size;
    struct sufs_ring_buffer_entry * kfs_requests;
    struct sufs_ring_buffer_entry * libfs_requests;
    char pad1[40];

    /* Second cache line: consumer index */
    int comsumer_idx;
    char pad2[60];

    /* Third cache line: producer index */
    int producer_idx;
    char pad3[60];

    /* Last cache line: spinlock */
    volatile int spinlock;
    char pad4[60];
};



#endif
