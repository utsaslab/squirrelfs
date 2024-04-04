#ifndef SUFS_LIBFS_UTIL_H_
#define SUFS_LIBFS_UTIL_H_

#include <sched.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "../../include/libfs_config.h"

#define PAGE_ROUND_UP(a)                                                     \
     ((((unsigned long) (a)) + SUFS_PAGE_SIZE - 1) & ~(SUFS_PAGE_SIZE - 1))

#define PAGE_ROUND_DOWN(a)                                                   \
     ((((unsigned long) (a)) & ~(SUFS_PAGE_SIZE - 1)))

#define PAGE_OFFSET(a)                                                       \
    (((unsigned long) (a)) & ((1 << SUFS_PAGE_SHIFT) - 1))

#define CACHE_ROUND_UP(a)                                                     \
     ((((unsigned long) (a)) + SUFS_CACHELINE - 1) & ~(SUFS_CACHELINE - 1))

#define CACHE_ROUND_DOWN(a)                                                   \
     ((((unsigned long) (a)) & ~(SUFS_CACHELINE - 1)))

#define FILE_BLOCK_ROUND_UP(a)                                                \
     ((((unsigned long) (a)) + SUFS_FILE_BLOCK_SIZE - 1) & ~(SUFS_FILE_BLOCK_SIZE - 1))

#define FILE_BLOCK_ROUND_DOWN(a)                                               \
     ((((unsigned long) (a)) & ~(SUFS_FILE_BLOCK_SIZE - 1)))

#define FILE_BLOCK_OFFSET(a)                                                   \
    (((unsigned long) (a)) & ((1 << SUFS_FILE_BLOCK_SHIFT) - 1))

void sufs_libfs_pin_to_core(int core);

int getcpu(unsigned *cpu, unsigned *node);

static inline int sufs_libfs_getcpu(unsigned int * cpu,
        unsigned int * node)
{
    return getcpu(cpu, node);
}

static inline unsigned int sufs_libfs_current_cpu(void)
{
    unsigned int cpu = 0;

    getcpu(&cpu, NULL);

    return cpu;
}


static inline unsigned int sufs_libfs_current_node(void)
{
    unsigned int node = 0;

    getcpu(NULL, &node);

    return node;
}


static inline long long sufs_libfs_rdtsc(void)
{
    unsigned long hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return (lo | (hi << 32));
}

static inline unsigned long sufs_libfs_rdtscp(void)
{
    unsigned long rax, rdx;
    __asm__ __volatile__("rdtscp\n" : "=a"(rax), "=d"(rdx) : : "%ecx");
    return (rdx << 32) + rax;
}


static inline int sufs_libfs_gettid(void)
{
    /* return sched_getcpu(); */
    return syscall(SYS_gettid);
}

#endif
