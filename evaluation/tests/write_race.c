#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <string.h>
#include <signal.h>

#define BUFSIZE 1024 * 1024
#define IO_SIZE 4096

pthread_t thread1, thread2, thread3, thread4;

void *write_to_file(void *arg)
{
    char buf[BUFSIZE];
    int ret, fd, bytes_written;
    pid_t tid = syscall(__NR_gettid);

    memset(buf, 'a', BUFSIZE);

    while (1)
    {
        // printf("[%d] opening file\n", tid);
        fd = open("/mnt/pmem/write_race", O_RDWR | O_CREAT, 0777);
        if (fd < 0)
        {
            perror("open");
            return NULL;
        }
        bytes_written = 0;
        // printf("[%d] writing to file\n", tid);
        while (bytes_written < BUFSIZE)
        {
            printf("[%d] write started\n", tid);
            ret = write(fd, buf, IO_SIZE);
            printf("[%d] write finished\n", tid);
            if (ret < 0)
            {
                perror("write");
                close(fd);
                return NULL;
            }
            bytes_written += ret;
        }
        // printf("[%d] closing file\n", tid);
        close(fd);
    }

    return NULL;
}

void *read_from_file(void *arg)
{
    char buf[BUFSIZE];
    int ret, fd, bytes_read;
    pid_t tid = syscall(__NR_gettid);

    while (1)
    {
        printf("[%d] opening file\n", tid);
        fd = open("/mnt/pmem/write_race", O_RDONLY | O_CREAT, 0777);
        if (fd < 0)
        {
            perror("open");
            return NULL;
        }

        bytes_read = 0;
        printf("[%d] reading from file\n", tid);
        while (bytes_read < BUFSIZE)
        {
            printf("[%d] read started\n", tid);
            ret = read(fd, buf, BUFSIZE - bytes_read);
            printf("[%d] read finished\n", tid);
            if (ret < 0)
            {
                perror("read");
                close(fd);
                return NULL;
            }
            bytes_read += ret;
        }
        printf("[%d] closing file\n", tid);
        close(fd);
    }
    return NULL;
}

void sig_handler(int signum)
{
    printf("caught signal %d, exiting\n", signum);
    // pthread_join(thread1, NULL);
    // printf("thread 1 finished\n");
    // pthread_join(thread2, NULL);
    // printf("thread 2 finished\n");
    // pthread_join(thread3, NULL);
    // printf("thread 3 finished\n");
    // pthread_join(thread4, NULL);
    // printf("thread 4 finished\n");
    exit(0);
}

int main(void)
{

    int ret;

    signal(SIGINT, sig_handler);

    ret = pthread_create(&thread1, NULL, write_to_file, NULL);
    if (ret < 0)
    {
        perror("pthread create 1");
    }
    ret = pthread_create(&thread2, NULL, read_from_file, NULL);
    if (ret < 0)
    {
        perror("pthread create 2");
    }
    // ret = pthread_create(&thread3, NULL, read_from_file, NULL);
    // if (ret < 0) {
    //     perror("pthread create 3");
    // }
    // ret = pthread_create(&thread4, NULL, read_from_file, NULL);
    // if (ret < 0) {
    //     perror("pthread create 4");
    // }

    pthread_join(thread1, NULL);
    printf("thread 1 finished\n");
    pthread_join(thread2, NULL);
    printf("thread 2 finished\n");
    // pthread_join(thread3, NULL);
    // printf("thread 3 finished\n");
    // pthread_join(thread4, NULL);
    // printf("thread 4 finished\n");

    return 0;
}
