/*
 * Mostly copied from the journal.c in NOVA
 */


#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "../include/libfs_config.h"
#include "journal.h"
#include "super.h"
#include "cmd.h"
#include "balloc.h"

pthread_spinlock_t * sufs_libfs_journal_locks = NULL;

/**************************** Lite journal ******************************/

/*
 * Get the next journal entry.  Journal entries are stored in a circular
 * buffer.  They live a 1-page circular buffer.
 */
static inline unsigned long next_lite_journal(unsigned long curr_p)
{
    size_t size = sizeof(struct sufs_libfs_journal_entry);

    if ((curr_p & (SUFS_PAGE_SIZE - 1)) + size >= SUFS_PAGE_SIZE)
        return (curr_p & SUFS_PAGE_MASK);

    return curr_p + size;
}

/**************************** Create/commit ******************************/


/* Create and append an undo entry for a small update to PMEM. */
static unsigned long sufs_libfs_append_entry_journal(unsigned long curr_p,
        void *field)
{
    struct sufs_libfs_journal_entry * entry = NULL;
    unsigned long * aligned_field = NULL;
    unsigned long offset = 0;

    entry = (struct sufs_libfs_journal_entry *)
            sufs_libfs_offset_to_virt_addr(curr_p);

#if 0
    printf("curr_p: %lx\n", curr_p);
#endif

    /* Align to 8 bytes */
    aligned_field = (unsigned long *)((unsigned long)field & ~7UL);

    /* Store the offset instead of the pointer */
    offset = sufs_libfs_virt_addr_to_offset((unsigned long) aligned_field);
    entry->data1 = offset;
    entry->data2 = *aligned_field;

    curr_p = next_lite_journal(curr_p);

    return curr_p;
}


void sufs_libfs_flush_journal(unsigned long head, unsigned long tail)
{
    /* flush journal log entries in batch */
    if (head < tail)
    {
        struct sufs_libfs_journal_entry * entry =
                (struct sufs_libfs_journal_entry *) sufs_libfs_offset_to_virt_addr(head);

        sufs_libfs_clwb_buffer((void *) entry, tail - head);

    }
    /* circular */
    else
    {
        struct sufs_libfs_journal_entry * entry =
                (struct sufs_libfs_journal_entry *) sufs_libfs_offset_to_virt_addr(head);

        /* head to end */
        sufs_libfs_clwb_buffer((void * ) entry,
                SUFS_PAGE_SIZE - (head & ~SUFS_PAGE_MASK));

        entry = (struct sufs_libfs_journal_entry *)
                sufs_libfs_offset_to_virt_addr(tail);

        /* start to tail */
        sufs_libfs_clwb_buffer((void*)(((unsigned long) entry) & SUFS_PAGE_MASK),
            tail & (~SUFS_PAGE_MASK));
    }

    /* Given TSO, this seems not needed */
    /* sufs_libfs_sfence(); */
}

/* Journaled transactions for rename operations */
unsigned long sufs_libfs_create_rename_transaction(int cpu,
        void * new_dir_name_len, void * old_dir_ino, void * rb_dir_ino)
{
    struct sufs_libfs_journal_ptr_pair *pair = NULL;
    unsigned long temp = 0;

    pair = sufs_libfs_get_journal_pointers(&sufs_libfs_sb, cpu);

    if (pair->journal_head == 0 ||
            pair->journal_head != pair->journal_tail)
       abort();

    temp = pair->journal_head;

    temp = sufs_libfs_append_entry_journal(temp, new_dir_name_len);
    temp = sufs_libfs_append_entry_journal(temp, old_dir_ino);

    if (rb_dir_ino)
        temp = sufs_libfs_append_entry_journal(temp, old_dir_ino);

    sufs_libfs_flush_journal(pair->journal_head, temp);

    pair->journal_tail = temp;
    sufs_libfs_clwb_buffer(&pair->journal_head, sizeof(pair->journal_head));
    sufs_libfs_sfence();

    return temp;
}


/* Commit the transactions by dropping the journal entries */
void sufs_libfs_commit_lite_transaction(int cpu, unsigned long tail)
{
    struct sufs_libfs_journal_ptr_pair * pair = NULL;

    pair = sufs_libfs_get_journal_pointers(&sufs_libfs_sb,
            cpu);

    if (pair->journal_tail != tail)
    {
        fprintf(stderr, "journal_tail is not tail!\n");
        abort();
    }

    pair->journal_head = tail;

    sufs_libfs_clwb_buffer(&pair->journal_head, sizeof(pair->journal_head));

    /* Given TSO, this seems not needed */
    sufs_libfs_sfence();
}

/**************************** Initialization ******************************/

/* Initialized DRAM journal state, validate */
void sufs_libfs_lite_journal_soft_init(struct sufs_libfs_super_block *sb)
{
    int i = 0, cpus = 0;

    // cpus = sb->cpus_per_socket * sb->pm_nodes;
    // FIXME: have to hardcode because we only use one node but there are actually two
    cpus = sb->cpus_per_socket * 2;

    sufs_libfs_journal_locks = calloc(cpus, sizeof(pthread_spinlock_t));

    if (!sufs_libfs_journal_locks)
    {
        fprintf(stderr, "Allocate journal locks failed!\n");
        abort();
    }

    for (i = 0; i < cpus; i++)
        pthread_spin_init(&(sufs_libfs_journal_locks[i]), PTHREAD_PROCESS_SHARED);
}

/* Initialized persistent journal state */
void sufs_libfs_lite_journal_hard_init(struct sufs_libfs_super_block *sb)
{
    struct sufs_libfs_journal_ptr_pair *pair = NULL;
    int i;
    unsigned long block = 0, orig_nums = 0, nums = 0;
    unsigned int cpu = 0, node = 0, cpus = 0;

    sufs_libfs_getcpu(&cpu, &node);

    cpus = sb->cpus_per_socket * sb->sockets;

    /*
     * First part is the pages to store journal pointer
     * Second part is the pages to store the journal content
     */
    orig_nums = (PAGE_ROUND_UP(cpus * SUFS_CACHELINE) / SUFS_PAGE_SIZE);

    nums = orig_nums;

    if ( (sufs_libfs_cmd_alloc_blocks(&block, &nums, cpu, node) < 0) ||
            (nums != orig_nums) )
    {
        fprintf(stderr, "Allocate block failed!\n");
        abort();
    }

    sb->journal_addr = sufs_libfs_block_to_virt_addr(block);

    for (i = 0; i < cpus; i++)
    {
        unsigned long content_addr = 0;
        pair = sufs_libfs_get_journal_pointers(sb, i);

        /* allocating the CPU's journal on its own NUMA node*/
        nums = 1;
        if ( (sufs_libfs_cmd_alloc_blocks(&block, &nums, i, i / sb->cpus_per_socket) < 0) ||
                (nums != 1) )
        {
            fprintf(stderr, "Allocate block failed!\n");
            abort();
        }

        content_addr = sufs_libfs_block_to_virt_addr(block);

        pair->journal_head = pair->journal_tail =
                sufs_libfs_virt_addr_to_offset(content_addr);
#if 0
        printf("journal_head: %lx, journal_tail: %lx\n", pair->journal_head,
                pair->journal_tail);
#endif

        sufs_libfs_clwb_buffer(pair, sizeof(struct sufs_libfs_journal_ptr_pair));
    }

    sufs_libfs_sfence();
    return sufs_libfs_lite_journal_soft_init(sb);
}

