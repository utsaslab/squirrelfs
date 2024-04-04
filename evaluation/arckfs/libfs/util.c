#define _GNU_SOURCE
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>

#include "../include/libfs_config.h"
#include "util.h"

void sufs_libfs_pin_to_core(int core)
{
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(core, &mask);

    if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1)
    {
        perror("sched_setaffinity");
        abort();
    }
}

