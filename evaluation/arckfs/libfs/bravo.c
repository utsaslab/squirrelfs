#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "../include/libfs_config.h"
#include "compiler.h"
#include "bravo.h"
#include "util.h"

volatile unsigned long ** sufs_libfs_global_vr_table;

void sufs_libfs_init_global_rglock_bravo(void)
{
    sufs_libfs_global_vr_table = calloc(SUFS_LIBFS_RL_TABLE_SIZE, sizeof(unsigned long *));
}

void sufs_libfs_free_global_rglock_bravo(void)
{
    if (sufs_libfs_global_vr_table)
        free(sufs_libfs_global_vr_table);

    sufs_libfs_global_vr_table = NULL;
}





