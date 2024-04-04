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

const int READ_SIZE = 1024*64;
const int WRITE_SIZE = 1187;
const int FILE_SIZE = 1024*1024*512;

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
    char *buffer = malloc(FILE_SIZE*iterations);

    srand(time(NULL));

    // fd = open("/mnt/pmem/foo", O_RDWR);
    // if (fd < 0) {
    //     perror("open");;
    //     return fd;
    // }

    // ret = fstat(fd, &st);
    // write_file_size = st.st_size;
    // printf("write file size: %d\n", write_file_size);

    fd_read = open("/mnt/pmem/read", O_RDWR | O_CREAT);
    if (fd_read < 0) {
        perror("open");
        return fd_read;
    }

    int bytes = 0;
    while (bytes < FILE_SIZE) {
        ret = write(fd_read, buffer, WRITE_SIZE);
        if (ret < 0) {
            perror("write");
            close(fd_read);
            return ret;
        }
        bytes += ret;
    }
    // ret = write(fd_read, buffer, FILE_SIZE*iterations);
    // if (ret < 0) {
    //     perror("write");
    //     // close(fd);
    //     close(fd_read);
    //     return ret;
    // }
    // close(fd_read);

    fd_read = open("/mnt/pmem/read", O_RDWR);
    if (fd_read < 0) {
        perror("open");
        return fd_read;
    }


    for (int i = 0; i < iterations; i++) {
        // read 
        offset = rand() % (FILE_SIZE - READ_SIZE);
        ret = pread(fd_read, read_data, READ_SIZE, offset);
        if (ret < 0) {
            perror("read");
            // close(fd);
            return ret;
        }
    }

    // close(fd);
    close(fd_read);
    free(buffer);

    return 0;
}