#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

// attempts to emulate some of the performance conditions of varmail

const int WRITE_SIZE = 4096;

int main(void) {
    char data[WRITE_SIZE];
    char read_data[WRITE_SIZE];
    int ret;
    memset(data, 'a', WRITE_SIZE);
    memset(read_data, 0, WRITE_SIZE);

    for (int j = 0; j < 200000; j++) {
        int fd = open("/mnt/pmem/foo", O_RDWR | O_CREAT);
        assert(fd > 0);

        for (int i = 0; i < 2; i++) {
            int bytes_written = 0; 
            while (bytes_written < WRITE_SIZE) {
                ret = write(fd, data, WRITE_SIZE - bytes_written);
                bytes_written += ret;
            }
        }
        lseek(fd, 0, SEEK_SET);
        fsync(fd);

        for (int i = 0; i < 2; i++) {
            // ret = pread(fd, read_data, 8192, i*8192);
            int bytes_written = 0; 
            while (bytes_written < WRITE_SIZE) {
                ret = write(fd, data, WRITE_SIZE - bytes_written);
                bytes_written += ret;
            }
            // assert(ret == 4096);
            // assert(strcmp(data, read_data) == 0);
        }    
        close(fd);
        unlink("/mnt/pmem/foo");
    }
    
    return 0;
}