#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <assert.h>
#include <stdlib.h>

const unsigned long ITERATIONS = 25000;

int main(void) {
    char filename[1024];
    struct timeval tv_start, tv_end;
    long start, end;
    unsigned long creat_latency_sum = 0;

    for (int i = 0; i < ITERATIONS; i++) {
        snprintf(filename, 1024, "/mnt/pmem/creat_%d", i);
        
        gettimeofday(&tv_start, NULL);
        int fd = open(filename, O_CREAT);
        fsync(fd);
        gettimeofday(&tv_end, NULL);
        if (fd < 0) {
            perror("open");
            return fd;
        }
        close(fd);
        unlink(filename);

        // microseconds
        start = (unsigned long)tv_start.tv_sec * 1000000 + (unsigned long)tv_start.tv_usec;
        end = (unsigned long)tv_end.tv_sec * 1000000 + (unsigned long)tv_end.tv_usec;
        creat_latency_sum += (end - start);
    }

    unsigned long avg_creat_latency = creat_latency_sum / ITERATIONS;
    printf("avg creat latency(us): %ld\n", avg_creat_latency);

    return 0;
}