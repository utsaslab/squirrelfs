#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define BUFSIZE 1024*1024*4

int main(void) {
    int fd, bytes_written, ret, bytes_read, iterations;
    char buf[BUFSIZE];
    memset(buf, 'a', BUFSIZE);

    fd = open("/mnt/pmem/read_breakdown_test", O_RDWR | O_CREAT);
    if (fd < 0) {
        perror("open");
        return fd;
    }

    bytes_written = 0; 
    while (bytes_written < BUFSIZE) {
        ret = write(fd, buf, BUFSIZE);
        if (ret < 0) {
            perror("write");
            close(fd);
            return ret;
        }
        bytes_written += ret;
    }

    ret = lseek(fd, 0, SEEK_SET);
    if (ret < 0) {
        perror("lseek");
        close(fd);
        return ret;
    }

    bytes_read = 0;
    iterations = 0;
    while (bytes_read < BUFSIZE) {
        iterations++;
        ret = read(fd, buf, BUFSIZE);
        if (ret < 0) {
            perror("read");
            close(fd);
            return ret;
        }
        bytes_read += ret;
    }

    printf("Took %d iterations to read the file\n", iterations);

    close(fd);

    return 0;
}