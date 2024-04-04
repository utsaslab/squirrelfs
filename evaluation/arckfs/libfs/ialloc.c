#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

#include "../include/libfs_config.h"
#include "ialloc.h"
#include "random.h"
#include "cmd.h"

/* a bit map indicates whether the inode is allocated or not
 * set: allocated
 *
 * init: clear
 * allocate: set
 * free or unmap: clear
 */
atomic_char * sufs_libfs_inode_alloc_map = NULL;

static void sufs_libfs_init_ialloc_map(void)
{
    sufs_libfs_inode_alloc_map = calloc(1, SUFS_MAX_INODE_NUM / sizeof(char));

    if (!sufs_libfs_inode_alloc_map)
    {
        fprintf(stderr, "Cannot allocate inode map!\n");
        abort();
    }
}

/* init */
void sufs_libfs_alloc_inode_free_lists(struct sufs_libfs_super_block * sb)
{
    struct sufs_libfs_inode_free_list * ilist = NULL;
    int i = 0;

    sb->inode_free_lists = calloc(sb->sockets * sb->cpus_per_socket,
            sizeof(struct sufs_libfs_inode_free_list));

    if (!sb->inode_free_lists)
    {
        fprintf(stderr, "%s: Allocating inode maps failed.", __func__);
        abort();
    }

    sufs_libfs_init_ialloc_map();

    for (i = 0; i < (sb->sockets * sb->cpus_per_socket); i++)
    {
        ilist = &(sb->inode_free_lists[i]);
        ilist->free_inode_head = NULL;
        ilist->free_kfs_head = NULL;
        pthread_spin_init(&ilist->lock, PTHREAD_PROCESS_SHARED);
    }
}


/* Free */
static void
sufs_libfs_free_kfs_inodes(int ino, int num)
{
    int i = ino, start = ino, end = ino + num;

    while (sufs_libfs_is_inode_allocated(i) && i < end)
        i++;

    start = i;

    while (i < end)
    {
        if (sufs_libfs_is_inode_allocated(i))
        {
            sufs_libfs_cmd_free_inodes(start, i - start);

            while (sufs_libfs_is_inode_allocated(i) && i < end)
                i++;

            start = i;
        }

        i++;
    }

    if (start < end)
        sufs_libfs_cmd_free_inodes(start, end - start);
}

static void
sufs_libfs_free_one_inode_free_list(struct sufs_libfs_inode_free_list * ilist)
{
    struct sufs_libfs_kfs_inodes * iter_kfs = ilist->free_kfs_head;
    struct sufs_libfs_kfs_inodes * prev_kfs = NULL;

    struct sufs_libfs_inode_free_item * iter_inode = ilist->free_inode_head;
    struct sufs_libfs_inode_free_item * prev_inode = NULL;

    while (iter_kfs != NULL)
    {
        sufs_libfs_free_kfs_inodes(iter_kfs->ino, iter_kfs->num);

        prev_kfs = iter_kfs;
        iter_kfs = iter_kfs->next;

        free(prev_kfs);
    }

    while (iter_inode != NULL)
    {
        prev_inode = iter_inode;
        iter_inode = iter_inode->next;

        free(prev_inode);
    }
}

void sufs_libfs_free_inode_free_lists(struct sufs_libfs_super_block * sb)
{
    struct sufs_libfs_inode_free_list * ilist = NULL;
    int i = 0;

    for (i = 0; i < (sb->sockets * sb->cpus_per_socket); i++)
    {
        ilist = &(sb->inode_free_lists[i]);

        if (ilist)
        {
            sufs_libfs_free_one_inode_free_list(ilist);
        }
    }

    if (sb->inode_free_lists)
        free(sb->inode_free_lists);

    sb->inode_free_lists = NULL;
}

static void sufs_libfs_add_inode_free_lists(
        struct sufs_libfs_inode_free_list *free_list,
        int start, unsigned long tot)
{
    int i = 0;

    struct sufs_libfs_kfs_inodes * kfs_inodes =
            malloc(sizeof(struct sufs_libfs_kfs_inodes));

    kfs_inodes->ino = start;
    kfs_inodes->num = tot;
    kfs_inodes->next = free_list->free_kfs_head;
    free_list->free_kfs_head = kfs_inodes;

    for (i = tot - 1; i >= 0; i--)
    {
        struct sufs_libfs_inode_free_item * item =
                malloc(sizeof(struct sufs_libfs_inode_free_item));

        item->ino = start + i;
        item->next = free_list->free_inode_head;
        free_list->free_inode_head = item;
    }
}

static void __sufs_libfs_alloc_inode_from_kernel(
        struct sufs_libfs_super_block * sb,
        struct sufs_libfs_inode_free_list *free_list,
        int cpu)
{

    int inode = 0, num = SUFS_LIBFS_INODE_CHUNK, ret = 0;

    ret = sufs_libfs_cmd_alloc_inodes(&inode, &num, cpu);

    if (ret < 0)
    {
        fprintf(stderr, "alloc inode: num %d cpu %d failed\n", num, cpu);
        return;
    }

#if 0
    printf("allocated inode: ino %d, num %d, cpu: %d\n", inode, num, cpu);
#endif

    sufs_libfs_add_inode_free_lists(free_list, inode, num);
}

int sufs_libfs_init_inode_free_lists(struct sufs_libfs_super_block *sb)
{
    int i = 0, cpus = 0;

    struct sufs_libfs_inode_free_list * ilist = NULL;

    cpus = sb->sockets * sb->cpus_per_socket;

    for (i = 0; i < cpus; i++)
    {
        ilist = &(sb->inode_free_lists[i]);
        __sufs_libfs_alloc_inode_from_kernel(sb, ilist, i);
    }

    return 0;
}

/*
 * Which cpu this inode belongs to?
 * Choose a random CPU to return the inode
 */
static inline int sufs_libfs_inode_to_cpu(struct sufs_libfs_super_block * sb,
        int ino)
{
    int ret = 0;

    ret = sufs_libfs_xor_random() % (sb->cpus_per_socket * sb->sockets);

    return ret;
}

int sufs_libfs_free_inode(struct sufs_libfs_super_block * sb, int ino)
{
    int cpu = 0;

    cpu = sufs_libfs_inode_to_cpu(sb, ino);

    struct sufs_libfs_inode_free_list * free_list = NULL;

    struct sufs_libfs_inode_free_item * item =
            malloc(sizeof(struct sufs_libfs_inode_free_item));

    free_list = &(sb->inode_free_lists[cpu]);

    pthread_spin_lock(&free_list->lock);

    item->ino = ino;

    item->next = free_list->free_inode_head;

    free_list->free_inode_head = item;

    pthread_spin_unlock(&free_list->lock);

    sufs_libfs_inode_clear_allocated(ino);

#if 0
    printf("self: %lx, free inode: %d\n", pthread_self(), ino);
    /* fflush(stdout); */
#endif

    return 0;
}


int sufs_libfs_new_inode(struct sufs_libfs_super_block * sb, int cpu)
{
    struct sufs_libfs_inode_free_list *free_list = NULL;
    int ret = 0;

    free_list = &(sb->inode_free_lists[cpu]);
    pthread_spin_lock(&free_list->lock);

    /* empty */
    if (free_list->free_inode_head == NULL)
    {
        __sufs_libfs_alloc_inode_from_kernel(sb, free_list, cpu);

        if (free_list->free_inode_head == NULL)
        {
            pthread_spin_unlock(&free_list->lock);
            return 0;
        }
    }

    ret = free_list->free_inode_head->ino;

    free_list->free_inode_head = free_list->free_inode_head->next;

    pthread_spin_unlock(&free_list->lock);

    sufs_libfs_inode_set_allocated(ret);

#if 0
    printf("self: %lx, allocate inode: %d\n", pthread_self(), ret);
    /* fflush(stdout); */
#endif

    return ret;
}
