#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "ioctl.h"

int main(void)
{

    int fd = open("/mnt/pmem/foo", O_RDWR | O_CREAT);
    if (fd < 0)
    {
        perror("fd");
        return fd;
    }

    int ret = ioctl(fd, PRINT_TIMING, NULL);
    if (ret < 0)
    {
        perror("ioctl");
    }

    ret = ioctl(fd, CLEAR_TIMING, NULL);
    if (ret < 0)
    {
        perror("ioctl");
    }

    close(fd);

    return 0;
}