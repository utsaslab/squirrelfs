#define _GNU_SOURCE 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sched.h>

int main(void) {
    char* buffer;
    int ret;
    buffer = malloc(1024);
    memset(buffer, 0, 1024);

    cpu_set_t cpu_set;

    CPU_SET(0, &cpu_set);

    ret = sched_setaffinity(0, sizeof(cpu_set), &cpu_set);
    if (ret < 0) {
        perror("sched_setaffinity");
        return ret;
    }

    ret = mount("/dev/pmem0", "/mnt/pmem", "squirrelfs", 0, "init");
    if (ret < 0) {
        perror("mount");
        return ret;
    }

    int fd = open("/mnt/pmem/foo", O_CREAT | O_RDWR);
    if (fd < 0) {
        perror("open");
        return fd;
    }

    for (int i = 0; i < 50; i++) {
        ret = write(fd, buffer, 1024);
        if (ret < 0) {
            perror("write");
            close(fd);
            return ret;
        }
    }
    close(fd);

    ret = umount("/mnt/pmem");
    if (ret < 0) {
        perror("umount");
        return ret;
    }

    ret = mount("/dev/pmem0", "/mnt/pmem", "squirrelfs", 0, NULL);
    if (ret < 0) {
        perror("mount");
        return ret;
    }

    ret = umount("/mnt/pmem");
    if (ret < 0) {
        perror("umount");
        return ret;
    }



    return 0;
}