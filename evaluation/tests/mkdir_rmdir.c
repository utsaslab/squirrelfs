#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

int main(void) {
    int ret, fd;
    char filename[64];
    memset(filename, 0, 64);

    ret = mkdir("/mnt/pmem/dir", 0777);
    if (ret < 0) {
        perror("mkdir");
        return ret;
    }

    for (int i = 0; i < 4096; i++) {
        sprintf(filename, "/mnt/pmem/dir/%d", i);
        fd = open(filename, O_CREAT);
        if (fd < 0) {
            perror("open");
            return fd;
        }
        close(fd);

    }

    

    return 0;
}