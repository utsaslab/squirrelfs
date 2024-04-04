#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

int main(void) {
    int fd = open("/mnt/pmem/foo", O_RDWR | O_CREAT);
    assert(fd > 0);

    return 0;
}