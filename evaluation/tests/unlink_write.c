#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

int main(void) {
    int fd, ret;
    char buf[16];
    DIR* d;

    ret = mkdir("/mnt/pmem/foo", 0777);


    fd = open("/mnt/pmem/foo", O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        perror("open");
        return fd;
    }
    d = fdopendir(fd);
    if (!d) {
        perror("fopendir");
        return -1;
    }

    ret = rmdir("/mnt/pmem/foo");
    if (ret < 0) {
        perror("unlink");
        close(fd);
        return ret;
    }

    // ret = read(fd, buf, 1);
    struct dirent *dir = readdir(d);
    if (!dir) {
        perror("readdir");
        close(fd);
        return ret;
    }
    // printf("read %d bytes\n", ret);


    close(fd);


    return 0;
}