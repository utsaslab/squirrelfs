#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

// attempts to emulate some of the performance conditions of varmail

int main(void) {
    char data[4096];
    char read_data[1024*16];
    int ret;
    memset(data, 'a', 4096);
    data[4095] = 0;
    memset(read_data, 0, 1024*16);

    for (int j = 0; j < 200000; j++) {
        int fd = open("/mnt/pmem/foo", O_RDWR | O_CREAT);
        assert(fd > 0);

        for (int i = 0; i < 10; i++) {
            ret = write(fd, data, 4096);
            // printf("%i\n", ret);
            assert(ret == 4096);
        }
        lseek(fd, 0, SEEK_SET);
        fsync(fd);

        for (int i = 0; i < 5; i++) {
            ret = pread(fd, read_data, 8192, i*8192);
            // assert(ret == 4096);
            // assert(strcmp(data, read_data) == 0);
        }    
        close(fd);
        unlink("/mnt/pmem/foo");
    }
    
    return 0;
}