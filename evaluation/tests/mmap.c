#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>

const int MMAP_SIZE = 4096;

int main(void) {
    int *mmap_addr = NULL;
    int fd, ret;
    char *write_buf;

    fd = open("/mnt/pmem/foo", O_CREAT | O_RDWR, 0777);
    if (fd < 0) {
        perror("open");
        return fd;
    }

    write_buf = malloc(MMAP_SIZE);
    memset(write_buf, 'a', MMAP_SIZE);
    ret = write(fd, write_buf, MMAP_SIZE);

    mmap_addr = mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_SYNC, fd, 0);
    if (mmap_addr == MAP_FAILED) {
        printf("mmap failed\n");
        return -1;
    }
    perror("mmap");
    printf("%p\n", mmap_addr);

    mmap_addr[0] = 1;

    msync(mmap_addr, MMAP_SIZE, 0);

    close(fd);
    perror("close");

    munmap(mmap_addr, MMAP_SIZE);
    perror("munmap");


    return 0;
}
