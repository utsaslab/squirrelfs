#ifndef SUFS_KFS_UTIL_H_
#define SUFS_KFS_UTIL_H_

#include "../include/kfs_config.h"

static inline unsigned long sufs_kfs_rdtsc(void)
{
    unsigned long rax, rdx;
    __asm__ __volatile__("rdtsc\n" : "=a"(rax), "=d"(rdx));
    return (rdx << 32) + rax;
}

#define SUFS_KFS_PAGE_ROUND_UP(a)                                              \
     ((((unsigned long) (a)) + SUFS_PAGE_SIZE - 1) & ~(SUFS_PAGE_SIZE - 1))

#define SUFS_KFS_NEXT_PAGE(a)                                                  \
       ((((unsigned long) (a)) & SUFS_PAGE_MASK) + SUFS_PAGE_SIZE)

#define SUFS_KFS_FILE_BLOCK_OFFSET(a)                                          \
    (((unsigned long) (a)) & ((1 << SUFS_FILE_BLOCK_SHIFT) - 1))

#define SUFS_KFS_CACHE_ROUND_UP(a)                                             \
     ((((unsigned long) (a)) + SUFS_CACHELINE - 1) & ~(SUFS_CACHELINE - 1))

#define SUFS_KFS_CACHE_ROUND_DOWN(a)                                           \
     ((((unsigned long) (a)) & ~(SUFS_CACHELINE - 1)))


static inline void sufs_kfs_memset_nt(void *dest, unsigned int
        dword, unsigned long length)
{
    unsigned long dummy1, dummy2;
    unsigned long qword = ((unsigned long)dword << 32) | dword;

    asm volatile("movl %%edx,%%ecx\n"
             "andl $63,%%edx\n"
             "shrl $6,%%ecx\n"
             "jz 9f\n"
             "1:      movnti %%rax,(%%rdi)\n"
             "2:      movnti %%rax,1*8(%%rdi)\n"
             "3:      movnti %%rax,2*8(%%rdi)\n"
             "4:      movnti %%rax,3*8(%%rdi)\n"
             "5:      movnti %%rax,4*8(%%rdi)\n"
             "8:      movnti %%rax,5*8(%%rdi)\n"
             "7:      movnti %%rax,6*8(%%rdi)\n"
             "8:      movnti %%rax,7*8(%%rdi)\n"
             "leaq 64(%%rdi),%%rdi\n"
             "decl %%ecx\n"
             "jnz 1b\n"
             "9:     movl %%edx,%%ecx\n"
             "andl $7,%%edx\n"
             "shrl $3,%%ecx\n"
             "jz 11f\n"
             "10:     movnti %%rax,(%%rdi)\n"
             "leaq 8(%%rdi),%%rdi\n"
             "decl %%ecx\n"
             "jnz 10b\n"
             "11:     movl %%edx,%%ecx\n"
             "shrl $2,%%ecx\n"
             "jz 12f\n"
             "movnti %%eax,(%%rdi)\n"
             "12:\n"
             : "=D"(dummy1), "=d"(dummy2)
             : "D"(dest), "a"(qword), "d"(length)
             : "memory", "rcx");
}


static inline void sufs_kfs_mm_clwb(unsigned long addr)
{
#if SUFS_CLWB_FLUSH
    asm volatile(".byte 0x66; xsaveopt %0" : "+m"(*(volatile char *)(addr)));
#else
    asm volatile("clflush %0" : "+m"(*(volatile char *)(addr)));
#endif
}


static inline void sufs_kfs_sfence(void)
{
    asm volatile ("sfence\n" : : );
}

static inline void sufs_kfs_clwb_buffer(void * ptr, unsigned int len)
{
    unsigned int i = 0;

    /* align addr to cache line */
    unsigned long addr = SUFS_KFS_CACHE_ROUND_DOWN((unsigned long) ptr);

    /* align len to cache line */
    len = SUFS_KFS_CACHE_ROUND_UP(len);
    for (i = 0; i < len; i += SUFS_CACHELINE)
    {
        sufs_kfs_mm_clwb(addr + i);
    }
}

#endif /* SUFS_KFS_UTIL_H_ */
