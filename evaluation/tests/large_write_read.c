#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

int main(void) {
    char data[4096];
    char read_data[4096];
    int ret;
    memset(data, 'a', 4096);
    data[4095] = 0;
    memset(read_data, 0, 4096);

    int fd = open("/mnt/pmem/foo", O_RDWR | O_CREAT);
    assert(fd > 0);

    for (int i = 0; i < 200000; i++) {
        ret = write(fd, data, 4096);
        // printf("%i\n", ret);
        assert(ret == 4096);
    }
    lseek(fd, 0, SEEK_SET);

    for (int i = 0; i < 200000; i++) {
        ret = pread(fd, read_data, 4096, i*4096);
        assert(ret == 4096);
        assert(strcmp(data, read_data) == 0);
    }    
    return 0;
}