#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <fcntl.h>

int main(void) {
    struct stat statbuf;
    int ret, dirfd;

    ret = lstat("/mnt/pmem/bar", &statbuf);

    printf("File type:                ");

    switch (statbuf.st_mode & S_IFMT) {
        case S_IFBLK:  printf("block device\n");            break;
        case S_IFCHR:  printf("character device\n");        break;
        case S_IFDIR:  printf("directory\n");               break;
        case S_IFIFO:  printf("FIFO/pipe\n");               break;
        case S_IFLNK:  printf("symlink\n");                 break;
        case S_IFREG:  printf("regular file\n");            break;
        case S_IFSOCK: printf("socket\n");                  break;
        default:       printf("unknown?\n");                break;
    }

    printf("I-node number:            %ju\n", (uintmax_t) statbuf.st_ino);

    printf("Mode:                     %jo (octal)\n",
                  (uintmax_t) statbuf.st_mode);

    dirfd = open("/mnt/pmem/", O_DIRECTORY);
    ret = fstatat(dirfd, "/mnt/pmem/bar", &statbuf, 0);
    printf("File type:                ");

    switch (statbuf.st_mode & S_IFMT) {
        case S_IFBLK:  printf("block device\n");            break;
        case S_IFCHR:  printf("character device\n");        break;
        case S_IFDIR:  printf("directory\n");               break;
        case S_IFIFO:  printf("FIFO/pipe\n");               break;
        case S_IFLNK:  printf("symlink\n");                 break;
        case S_IFREG:  printf("regular file\n");            break;
        case S_IFSOCK: printf("socket\n");                  break;
        default:       printf("unknown?\n");                break;
    }

    printf("I-node number:            %ju\n", (uintmax_t) statbuf.st_ino);

    printf("Mode:                     %jo (octal)\n",
                  (uintmax_t) statbuf.st_mode);

    return 0;
}