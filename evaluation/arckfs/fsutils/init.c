#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "libutil.h"

int main(int argc, char * argv[])
{
    unsigned long * addr = 0;
    int fd = 0;
    int ret = 0;

    if ((fd = open(SUFS_DEV_PATH, O_RDWR)) == -1)
    {
        perror("open");
        exit(1);
    }

    /* ioctl returns an integer and overflows our return value */
    if ((ret = ioctl(fd, SUFS_CMD_DEBUG_INIT)) == -1)
    {
        perror("ioctl init");
        exit(1);
    }

    printf("ret is %d\n", ret);

    return 0;
}
