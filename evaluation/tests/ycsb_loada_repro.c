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
#include <pthread.h>

const int WRITE_SIZE = 1187;

int run_op(int iterations);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Please give number of iterations\n");
        return 1;
    }

    int iterations = atoi(argv[1]);
    if (iterations < 0) {
        printf("error converting %s to integer\n", argv[1]);
        return 1;
    }

    run_op(iterations);

    printf("Done!");

    return 0;
}

int run_op(int iterations) {
    int ret, fd;
    char data[WRITE_SIZE];

    fd = open("/mnt/pmem/foo", O_RDWR | O_CREAT);
    if (fd < 0) {
        perror("open");;
        return fd;
    }

    for (int i = 0; i < iterations; i++) {
        lseek(fd, 0, SEEK_END);
        ret = write(fd, data, WRITE_SIZE);
        if (ret < 0) {
            perror("write");
            close(fd);
            return ret;
        }
    }

    close(fd);

}