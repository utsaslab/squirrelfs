#include <stdbool.h> 
#include <stdio.h>
#include <assert.h> 
#include <sys/statvfs.h>
#include <string.h>
#define PAGESZ 4096

int main(void) {
    struct statvfs stat;
    assert(statvfs("/mnt/pmem", &stat) == 0);
    unsigned long pages_start = stat.f_bfree;

    char *path = "/mnt/pmem/myfile";

    char data[4096];
    memset(data, '\0', PAGESZ);
    int fd = open(path, O_RDWR | O_CREAT);
    assert(fd > 0);


    assert(statvfs("/mnt/pmem", &stat) == 0);
    unsigned long pages_after_create = stat.f_bfree; 
    assert (pages_start == pages_after_create + 1); // assert inode creation ccorrect

    const int num_pages = 200000; 
    for (int i = 0; i < num_pages; i++) {
        assert(write(fd, data, PAGESZ) == PAGESZ);
    }

    assert(statvfs("/mnt/pmem", &stat) == 0);
    unsigned long pages_now_free = stat.f_bfree; 
    assert(pages_after_create == pages_now_free + num_pages); // assert all pages are in use

    assert(lseek(fd, 0, SEEK_CUR) == (num_pages * PAGESZ)); // assert file expected size
    close(fd); 

    assert (remove(path) == 0);
    assert(statvfs("/mnt/pmem", &stat) == 0);
    unsigned long pages_end = stat.f_bfree; 
    assert(pages_start == pages_end); // assert all pages back in free list
    return 0; 
}