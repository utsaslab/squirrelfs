#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/time.h>

const unsigned long long FILE_SIZE = 1024 * 1024 * 1024;
const int BUF_SIZE = 1024 * 64;
const int READ_SIZE = BUF_SIZE / 4;
const int WRITE_SIZE = 1024 * 8;

int main(void) {
    int fd, ret, bytes_written, offset;
    char *buf = malloc(BUF_SIZE);

    fd = open("/mnt/pmem/foo", O_RDWR | O_CREAT);
    if (fd < 0) {
        perror("open");
        return fd;
    }
    printf("created file\n");

    bytes_written = 0;
    while (bytes_written < FILE_SIZE) {
        ret = write(fd, buf, WRITE_SIZE);
        if (ret < 0) {
            perror("write");
            close(fd);
            return ret;
        }
        bytes_written += ret;
    }
    printf("wrote to file\n");

    close(fd);

    for (int i = 0; i < 50; i++) {
        fd = open("/mnt/pmem/foo", O_RDWR);
        if (fd < 0) {
            perror("open");
            return fd;
        }

        offset = 0;
        while (offset < FILE_SIZE) {
            // ret = readahead(fd, offset, BUF_SIZE);
            // if (ret < 0) {
            //     perror("readahead");
            //     close(fd);
            //     return ret;
            // }
            read(fd, buf, READ_SIZE);
            read(fd, buf, READ_SIZE);
            read(fd, buf, READ_SIZE);
            read(fd, buf, READ_SIZE);
            offset += BUF_SIZE;
        }

        close(fd);  
    }

    
}