#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <linux/limits.h>

#define TEST_FILE       "/sufs/test"
#define MAX_DATA_SIZE   4096

char page_data_buffer[MAX_DATA_SIZE];


int fd = 0;
long current_file_size = 0, max_file_size = 0;

static void must_write(int fd, void * buf, unsigned long size)
{
    int already_write = 0;

    while (already_write != size)
    {
        int ret = 0;
        void * new_addr = (void *) ((unsigned long) buf + already_write);
        ret = write(fd, new_addr, size - already_write);

        if (ret == -1)
        {
            fprintf(stderr, "Cannot write to fd: %d, buf: %lx, size: %lu\n",
                    fd, (unsigned long) buf, size);

            fprintf(stderr, "reason: %s\n", strerror(errno));
            exit(1);

        }

        already_write += ret;
    }
}


static void generate_data(int size)
{
    long i = 0;

    for (i = 0; i < size; i++)
        page_data_buffer[i] = rand() % 26 + 'a';
}


static void do_add_to_end(void)
{
    lseek(fd, 0, SEEK_END);

    int size = rand() % MAX_DATA_SIZE + 1;

    generate_data(size);

    must_write(fd, page_data_buffer, size);

    current_file_size += size;
}

/*
 * First arg: iterations
 */
int main(int argc, char * argv[])
{
    unlink(TEST_FILE);

    fd = open(TEST_FILE, O_RDWR | O_CREAT, 0644);

    if (argc != 2)
    {
        printf("Usage: %s max_file_size\n", argv[0]);

        exit(1);
    }

    max_file_size = atol(argv[1]);

    srand(time(NULL));

    while (current_file_size < max_file_size)
    {
            do_add_to_end();
    }

    printf("Finally passed test!\n");
    fflush(stdout);


    return 0;
}
