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

const unsigned long FILESIZE = 4096;
const unsigned long ITERATIONS = 25000;

int main(void) {
    char filename[1024];
    char* data;
    struct timeval tv_start, tv_end;
    long start, end;
    unsigned long latency_sum = 0;
    data = malloc(FILESIZE);
    memset(data, 'a', FILESIZE);

    for (int i = 0; i < ITERATIONS; i++) {
        snprintf(filename, 1024, "/mnt/pmem/creat_%d", i);
        
        int fd = open(filename, O_CREAT);
        if (fd < 0) {
            perror("open");
            return fd;
        }
        write(fd, data, FILESIZE);
        close(fd);
    // }

    // for (int i = 0; i < ITERATIONS; i++) {
    //     snprintf(filename, 1024, "/mnt/pmem/creat_%d", i);

        gettimeofday(&tv_start, NULL);
        unlink(filename);
        gettimeofday(&tv_end, NULL);

        // microseconds
        start = (unsigned long)tv_start.tv_sec * 1000000 + (unsigned long)tv_start.tv_usec;
        end = (unsigned long)tv_end.tv_sec * 1000000 + (unsigned long)tv_end.tv_usec;
        latency_sum += (end - start);
    }

    unsigned long avg_latency = latency_sum / ITERATIONS;
    printf("avg unlink latency(us): %ld\n", avg_latency);

    return 0;
}