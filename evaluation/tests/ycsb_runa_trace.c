#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>

const int READ_SIZE = 4695;
const int WRITE_SIZE = 1187;
const int FILE_SIZE = 1024*1024;

int run_op(int iterations);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Please give number of iterations\n");
        return 1;
    }

    int iterations = atoi(argv[1]);
    if (iterations < 0) {
        printf("error converting %s to integer\n", argv[1]);
        return 1;
    }

    run_op(iterations);

    printf("Done!");

    return 0;
}

int run_op(int iterations) {
    int ret, fd, fd_read, offset, rw, size, write_file_size;
    char read_data[READ_SIZE];
    char write_data[WRITE_SIZE];
    struct stat st;
    char *buffer = malloc(FILE_SIZE);

    srand(time(NULL));

    fd = open("/mnt/pmem/foo", O_RDWR);
    if (fd < 0) {
        perror("open");;
        return fd;
    }

    ret = fstat(fd, &st);
    write_file_size = st.st_size;
    printf("write file size: %d\n", write_file_size);

    fd_read = open("/mnt/pmem/read", O_RDWR | O_CREAT);
    if (fd_read < 0) {
        perror("open");
        return fd_read;
    }
    ret = write(fd_read, buffer, FILE_SIZE);
    if (ret < 0) {
        perror("write");
        close(fd);
        close(fd_read);
        return ret;
    }


    for (int i = 0; i < iterations; i++) {
        rw = rand() % 2;
        if (rw == 0) {
            // read 
            offset = rand() % (FILE_SIZE - READ_SIZE);
            ret = pread(fd_read, read_data, READ_SIZE, offset);
            if (ret < 0) {
                perror("read");
                close(fd);
                return ret;
            }
        } else {
            // write 
            offset = rand() % (write_file_size - WRITE_SIZE);
            ret = pwrite(fd, write_data, WRITE_SIZE, offset);
            if (ret < 0) {
                perror("write");
                close(fd);
                return ret;
            }
        }
    }

    close(fd);
    close(fd_read);

    return 0;
}