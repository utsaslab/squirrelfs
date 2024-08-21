#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h> 
#include <stdio.h>
#include <assert.h> 
#define PAGESZ 4096
// consume all the pages and test effectiveness of the deallocator to make space for a new file 
// extend the size of a file or create a file and return the new size of the file
long int enlarge_file(char *path, long int size) {
    int fd = open(path, O_CREAT | O_RDWR); 
    assert (fd > -1); 
    lseek(fd, size, SEEK_SET); 
    FILE *fp = fdopen(fd, "w");
    assert(fp); 
    fputc('\0', fp);
    fclose(fp);
    close(fd);
    fd = open(path, O_CREAT | O_RDWR); 
    assert (fd > -1); 
    lseek(fd, 0L, SEEK_END); 
    long int new_size = lseek(fd, 0, SEEK_CUR) - 1; 
    close(fd);
    return new_size; 
}

int main(void) {
    bool used_all_pages = false;
    int multiple = 1; 
    long int prev_size = 0; 
    char *path = "/mnt/pmem/myfile";
    char *path2 = "/mnt/pmem/myfile2";
    while (!used_all_pages) {
        long int new_size = enlarge_file(path, multiple * PAGESZ); 
        used_all_pages = prev_size == new_size;
        multiple = used_all_pages ? multiple : multiple + 1; 
        prev_size = new_size; 
    }
    // a new file with one less page should be able to be allocated after removal 
    assert(prev_size > 0);
    assert (remove(path) == 0);
    long int new_size = enlarge_file(path2, PAGESZ * (multiple - 1)); 
    assert (remove(path2) == 0); 
    assert(new_size == PAGESZ * (multiple - 1)); 
    return 0; 
}
