#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/mount.h>

const int WRITE_BUF_SIZE = 1024*1024;
const int FILE_SIZE = 1024*1024;

int main(void) {
    int fd4kb, fd16kb, fd64kb, fd128kb, fd512kb, fd1mb, ret, bytes_written, write_size;
    void *addr4kb = NULL, *addr16kb = NULL, *addr64kb = NULL, *addr128kb = NULL, *addr512kb = NULL, *addr1mb = NULL; 
    char *write_buffer;
    struct timeval tv_start, tv_end;
    long start, end;

    fd4kb = open("/mnt/pmem/4kb", O_CREAT | O_RDWR);
    fd16kb = open("/mnt/pmem/16kb", O_CREAT | O_RDWR);
    fd64kb = open("/mnt/pmem/64kb", O_CREAT | O_RDWR);
    fd128kb = open("/mnt/pmem/128kb", O_CREAT | O_RDWR);
    fd512kb = open("/mnt/pmem/512kb", O_CREAT | O_RDWR);
    fd1mb = open("/mnt/pmem/1mb", O_CREAT | O_RDWR);

    write_buffer = malloc(WRITE_BUF_SIZE);

    addr4kb = mmap(NULL, FILE_SIZE, PROT_READ | PROT_WRITE, 0, fd4kb, 0);
    addr16kb = mmap(NULL, FILE_SIZE, PROT_READ | PROT_WRITE, 0, fd16kb, 0);
    addr64kb = mmap(NULL, FILE_SIZE, PROT_READ | PROT_WRITE, 0, fd64kb, 0);
    addr128kb = mmap(NULL, FILE_SIZE, PROT_READ | PROT_WRITE, 0, fd128kb, 0);
    addr512kb = mmap(NULL, FILE_SIZE, PROT_READ | PROT_WRITE, 0, fd512kb, 0);
    addr1mb = mmap(NULL, FILE_SIZE, PROT_READ | PROT_WRITE, 0, fd1mb, 0);


    munmap(addr4kb, FILE_SIZE);
    munmap(addr16kb, FILE_SIZE);
    munmap(addr64kb, FILE_SIZE);
    munmap(addr128kb, FILE_SIZE);
    munmap(addr512kb, FILE_SIZE);
    munmap(addr1mb, FILE_SIZE);

    bytes_written = 0;
    write_size = 4096;
    gettimeofday(&tv_start, NULL);
    while (bytes_written < FILE_SIZE) {
        memcpy(addr4kb, write_buffer, write_size);
        bytes_written += write_size;
    }
    gettimeofday(&tv_end, NULL);
    start = (unsigned long)tv_start.tv_sec * 1000000 + (unsigned long)tv_start.tv_usec;
    end = (unsigned long)tv_end.tv_sec * 1000000 + (unsigned long)tv_end.tv_usec;
    printf("4kb: %ld\n", end - start);

    close(fd4kb);
    close(fd16kb);
    close(fd64kb);
    close(fd128kb);
    close(fd512kb);
    close(fd1mb);


    return 0;
}