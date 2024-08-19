#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <pthread.h>
#include <string.h>

const int WRITE_BUF_SIZE = 131072;
const int READ_BUF_SIZE = 1024*1024;
const int APPEND_BUF_SIZE = 1024*16;
const int NUM_THREADS = 2;
const int ITERATIONS = 100000;

void *run_op(void *arg);

int main(void) {
    int ret;
    pthread_t threads[NUM_THREADS];
    int thread_nums[NUM_THREADS];
    int tids[NUM_THREADS];

    // ret = mount("/dev/pmem0", "/mnt/pmem/", "ext4", 0, "dax");
    // if (ret < 0) {
    //     perror("mount");
    //     return ret;
    // }
    // sleep(3);

    for (int i = 0; i < NUM_THREADS; i++) {
        thread_nums[i] = i;
        tids[i] = pthread_create(&threads[i], NULL, run_op, (void*)&thread_nums[i]);
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // umount("/mnt/pmem");

    // free(write_buf);
    // free(read_buf);
    // free(append_buf);
    return 0;
}

void *run_op(void *arg) {
    char filename[32];
    char *write_buf;//, *read_buf, *append_buf;
    int ret, fd, bytes;
    int thread_num = *(int*)arg;
    memset(filename, 0, 32);
    sprintf(filename, "/mnt/pmem/file%d", thread_num);
    printf("thread %d has file %s\n", thread_num, filename);

    write_buf = malloc(WRITE_BUF_SIZE);
    if (write_buf == NULL) {
        perror("malloc");
        return NULL;
    }

    // read_buf = malloc(READ_BUF_SIZE);
    // if (read_buf == NULL) {
    //     perror("malloc");
    //     return NULL;
    // }

    // append_buf = malloc(APPEND_BUF_SIZE);
    // if (append_buf == NULL) {
    //     perror("malloc");
    //     return NULL;
    // }

    for (int i = 0; i < ITERATIONS; i++) {
        fd = open(filename, O_CREAT | O_RDWR);
        if (fd < 0) {
            perror("open");
            free(write_buf);
            // free(read_buf);
            // free(append_buf);
            return NULL;
        }

        bytes = 0;
        while (bytes < WRITE_BUF_SIZE) {
            ret = write(fd, write_buf, WRITE_BUF_SIZE - bytes);
            if (ret < 0) {
                perror("write");
                close(fd);
                free(write_buf);
                // free(read_buf);
                // free(append_buf);
                return NULL;
            }
            bytes += ret;
        }
        

        close(fd);

        // fd = open(filename, O_RDWR);
        // if (fd < 0) {
        //     perror("open");
        //     free(write_buf);
        //     free(read_buf);
        //     free(append_buf);
        //     return NULL;
        // }

        // ret = lseek(fd, 0, SEEK_END);
        // if (ret < 0) {
        //     perror("lseek");
        //     close(fd);
        //     free(write_buf);
        //     free(read_buf);
        //     free(append_buf);
        //     return NULL;
        // }
        // bytes = 0;
        // while (bytes < APPEND_BUF_SIZE) {
        //     ret = write(fd, append_buf, APPEND_BUF_SIZE - bytes);
        //     if (ret < 0) {
        //         perror("append");
        //         close(fd);
        //         free(write_buf);
        //         free(read_buf);
        //         free(append_buf);
        //         return NULL;
        //     }
        //     bytes += ret;
        // }
        
        
        // close(fd);

        // fd = open(filename, O_RDWR);
        // if (fd < 0) {
        //     perror("open");
        //     free(write_buf);
        //     free(read_buf);
        //     free(append_buf);
        //     return NULL;
        // }

        // bytes = 0;
        // while (bytes < WRITE_BUF_SIZE + APPEND_BUF_SIZE) {
        //     ret = read(fd, read_buf, READ_BUF_SIZE);
        //     if (ret < 0) {
        //         perror("read");
        //         close(fd);
        //         free(write_buf);
        //         free(read_buf);
        //         free(append_buf);
        //         return NULL;
        //     }
        //     bytes += ret;
        // }

        // close(fd);

        ret = unlink(filename);
        if (ret < 0) {
            perror("unlink");
            printf("error unlinking file %s in %d\n", filename, thread_num);
            free(write_buf);
            // free(read_buf);
            // free(append_buf);
            return NULL;
        }
        
    }
}