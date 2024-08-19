#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

int main(void) {
    char data = 'a';
    // char read_data[10];
    int ret;
    // memset(data, 'a', 10);
    // // data[9] = 0;
    // memset(read_data, 0, 10);

    int fd = open("/mnt/pmem/foo", O_RDWR | O_CREAT);
    assert(fd > 0);

    for (int j = 0; j < 200000; j++) {
        for (int i = 0; i < 4096; i++) {
            ret = write(fd, &data, 1);
        }
        lseek(fd, 0, SEEK_SET);
    }
    
    // ret = pread(fd, read_data, 9, 0);
    // assert(ret == 9);
    // assert(strcmp(data, read_data) == 0);
    close(fd);

    return 0;
}