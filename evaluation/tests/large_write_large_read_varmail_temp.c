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

// attempts to emulate some of the performance conditions of varmail

const unsigned long FILESIZE = 16*1024*1024;
const unsigned long ITERATIONS = 2000;

int main(void) {
    // char data[FILESIZE];
    // char read_data[FILESIZE];
    char* data;
    char* read_data;
    int ret;
    struct timeval tv_start, tv_end;
    unsigned long write_latency_sum = 0;
    int write_count = 0;
    unsigned long read_latency_sum = 0;
    int read_count = 0;
    long start, end;
    data = malloc(FILESIZE);
    read_data = malloc(FILESIZE);
    memset(data, 'a', FILESIZE);
    memset(read_data, 0, FILESIZE);

    for (int j = 0; j < ITERATIONS; j++) {
        int fd = open("/mnt/pmem/foo", O_RDWR | O_CREAT);
        assert(fd > 0);

        gettimeofday(&tv_start, NULL);
        int bytes_written = 0;
        while (bytes_written < FILESIZE) {
            ret = write(fd, data, FILESIZE - bytes_written);
            assert(ret >= 0);
            bytes_written += ret;
        }
        gettimeofday(&tv_end, NULL);

        start = (long)tv_start.tv_sec * 1000000 + (long)tv_start.tv_usec;
        end = (long)tv_end.tv_sec * 1000000 + (long)tv_end.tv_usec;
        write_latency_sum += (end - start);
        write_count += 1;

        lseek(fd, 0, SEEK_SET);
        fsync(fd);

        gettimeofday(&tv_start, NULL);
        ret = pread(fd, read_data, FILESIZE, 0);
        gettimeofday(&tv_end, NULL);

        start = (long)tv_start.tv_sec * 1000000 + (long)tv_start.tv_usec;
        end = (long)tv_end.tv_sec * 1000000 + (long)tv_end.tv_usec;
        read_latency_sum += (end - start);
        read_count += 1;

        close(fd);
        unlink("/mnt/pmem/foo");
    }

    unsigned long avg_write_latency = write_latency_sum / write_count;
    unsigned long avg_read_latency = read_latency_sum / read_count;

    unsigned long write_throughput = ((FILESIZE * ITERATIONS) / 1024) / (write_latency_sum / 1000);
    unsigned long read_throughput = ((FILESIZE * ITERATIONS) / 1024) / (read_latency_sum / 1000);

    printf("%ld,%ld,%ld,%ld\n", avg_write_latency, avg_read_latency,write_throughput,read_throughput);

    free(data);
    free(read_data);
    
    return 0;
}