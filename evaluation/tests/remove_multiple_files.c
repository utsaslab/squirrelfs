#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h> 
#include <stdio.h>
#include <string.h>
#include <assert.h> 
#include <sys/statvfs.h>
#define PAGESZ 4096

// create many files and test whether removing will return all pages to free list
int main(void) {
    struct statvfs stat;
    assert(statvfs("/mnt/pmem", &stat) == 0);
    unsigned long pages_start = stat.f_bfree;
    unsigned num_files = 5000; 
    char filename[64];
    memset(filename, 0, 64);
    for (int i = 0; i < num_files; i ++) {
        sprintf(filename, "/mnt/pmem/%d", i);
        int fd = open(filename, O_CREAT | O_RDWR);
        lseek(fd, PAGESZ * 2, SEEK_SET); 
        FILE *fp = fdopen(fd, "w");
        assert(fp); 
        putc('\0', fp);
        fclose(fp);
        close(fd);
    }

    for (int i = 0; i < num_files; i ++) {
        sprintf(filename, "/mnt/pmem/%d", i);
        assert(remove(filename) == 0); 
    }
    assert(statvfs("/mnt/pmem", &stat) == 0);
    unsigned long pages_end = stat.f_bfree;
    assert(pages_start == pages_end); 
    return 0; 
}