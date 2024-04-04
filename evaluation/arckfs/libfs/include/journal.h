#ifndef SUFS_LIBFS_JOURNAL_H_
#define SUFS_LIBFS_JOURNAL_H_

/*
 * Mostly copied from NOVA
 */

#include "../../include/libfs_config.h"
#include "pthread.h"
#include "util.h"
#include "super.h"

static inline void sufs_libfs_mm_clwb(unsigned long addr)
{
#if SUFS_CLWB_FLUSH
    asm volatile(".byte 0x66; xsaveopt %0" : "+m" (*(volatile char *)(addr)));
#else
    asm volatile("clflush %0" : "+m"(*(volatile char *)(addr)));
#endif
}


static inline void sufs_libfs_sfence(void)
{
    asm volatile ("sfence\n" : : );
}

static inline void sufs_libfs_clwb_buffer(void * ptr, unsigned int len)
{
    unsigned int i = 0;

    /* align addr to cache line */
    unsigned long addr = CACHE_ROUND_DOWN((unsigned long) ptr);

    /* align len to cache line */
    len = CACHE_ROUND_UP(len);
    for (i = 0; i < len; i += SUFS_CACHELINE)
        sufs_libfs_mm_clwb(addr + i);
}


/* ======================= Lite journal ========================= */

/* Lightweight journal entry */
struct sufs_libfs_journal_entry {
    unsigned long data1;
    unsigned long data2;
};

/* Head and tail pointers into a circular queue of journal entries.  There's
 * one of these per CPU.
 */
struct sufs_libfs_journal_ptr_pair {
    unsigned long journal_head;
    unsigned long journal_tail;
};

extern pthread_spinlock_t * sufs_libfs_journal_locks;

static inline struct sufs_libfs_journal_ptr_pair *
sufs_libfs_get_journal_pointers(struct sufs_libfs_super_block *sb, int cpu)
{

    return (struct sufs_libfs_journal_ptr_pair *)
            (sb->journal_addr + cpu * SUFS_CACHELINE);
}

void sufs_libfs_flush_journal(unsigned long head, unsigned long tail);

void sufs_libfs_commit_lite_transaction(int cpu, unsigned long tail);

unsigned long sufs_libfs_create_rename_transaction(int cpu,
        void * new_dir_name_len, void * old_dir_ino, void * rb_dir_ino);

void sufs_libfs_lite_journal_soft_init(struct sufs_libfs_super_block *sb);

void sufs_libfs_lite_journal_hard_init(struct sufs_libfs_super_block *sb);

#endif
