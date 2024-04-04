#ifndef SUFS_KFS_AGENT_H_
#define SUFS_KFS_AGENT_H_

#include "../include/kfs_config.h"
#include "super.h"

struct sufs_kfs_agent_tasks {
    unsigned long kuaddr;
    unsigned long size;
};

void sufs_kfs_init_agents(struct sufs_super_block * sufs_sb);
void sufs_kfs_agents_fini(void);

extern int sufs_kfs_dele_thrds;
extern int sufs_kfs_agent_init;

extern struct mm_struct * sufs_kfs_mm;

extern unsigned long sufs_kfs_counter_addr[SUFS_MAX_THREADS];
extern struct page * sufs_kfs_counter_pg[SUFS_MAX_THREADS];

extern unsigned long sufs_kfs_buffer_ring_kaddr[SUFS_MAX_CPU];
extern struct page * sufs_kfs_buffer_ring_pg[SUFS_MAX_CPU];

#endif /* SUFS_KFS_AGENT_H_ */
