#define _GNU_SOURCE
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>


#include "../include/libfs_config.h"
#include "tls.h"
#include "util.h"
#include "compiler.h"

struct sufs_libfs_tls sufs_libfs_tls_data[SUFS_MAX_THREADS] __mpalign__;
__thread int sufs_libfs_my_thread = -1;

int sufs_libfs_btid = 0;

/* I cannot believe I am doing this */
void sufs_libfs_tls_init(void)
{
    sufs_libfs_btid = sufs_libfs_gettid();

#if 0
    printf("sizeof (struct) is %ld\n", sizeof(struct sufs_libfs_tls));
    printf("sizeof (array) is %ld\n", sizeof(sufs_libfs_tls_data));
    printf("addr of array is %lx\n", (unsigned long) sufs_libfs_tls_data);
    printf("addr of first element is %lx\n", (unsigned long) (&(sufs_libfs_tls_data[0])));
    printf("addr of second element is %lx\n", (unsigned long) (&(sufs_libfs_tls_data[1])));
#endif
    memset(sufs_libfs_tls_data, 0, sizeof(sufs_libfs_tls_data));
}


