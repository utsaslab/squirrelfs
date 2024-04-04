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

#define DATA_SIZE    4096
#define BASE_CHANCE   100

#define ADD_END          0
#define READ_MIDDLE      1
#define WRITE_MIDDLE     2

#define TEST_FILE       "/sufs/test"

char * page_data = NULL;
char page_data_buffer[DATA_SIZE];


int add_page_chance = 0, read_page_chance = 0, fd = 0;
long current_file_size = 0, max_file_size = 0, current_index = 0;

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

static void must_read(int fd, void * buf, unsigned long size)
{
    int already_read = 0;

    while (already_read != size)
    {
        int ret = 0;
        void * new_addr = (void *) ((unsigned long) buf + already_read);
        ret = read(fd, new_addr, size - already_read);

        if (ret == -1)
        {
            fprintf(stderr, "Cannot read from fd: %d, buf: %lx, size: %lu\n",
                    fd, (unsigned long) buf, size);

            fprintf(stderr, "reason: %s\n", strerror(errno));
            exit(1);
        }

        already_read += ret;
    }
}

static void generate_data(void)
{
    long i = 0;

    for (i = 0; i < DATA_SIZE; i++)
        page_data_buffer[i] = rand() % 26 + 'a';
}

static int decide_what_to_do(void)
{
    int chance = 0;

    if (current_file_size == 0)
        return ADD_END;

    if (current_file_size + DATA_SIZE >
            max_file_size)
        goto no_add;

    chance = rand() % BASE_CHANCE;

    if (chance <= add_page_chance)
        return ADD_END;

no_add:
    chance = rand() % BASE_CHANCE;

    if (chance <= read_page_chance)
        return READ_MIDDLE;
    else
        return WRITE_MIDDLE;
}

static void data_compare(unsigned long index)
{
    long i = 0;
    for (i = 0; i < DATA_SIZE; i++)
        if (page_data[index + i] != page_data_buffer[i])
        {
            printf("Failed test at index: %ld, offset: %ld!\n",
                    index/4096, i);
            printf("Written value: %d, read value: %d\n",
                    page_data[index + i], page_data_buffer[i]);

            fflush(stdout);

            abort();

            exit(1);
        }
}

static void do_add_to_end(void)
{
    lseek(fd, 0, SEEK_END);

    generate_data();

    must_write(fd, page_data_buffer, DATA_SIZE);

    memcpy(page_data + current_file_size,
            page_data_buffer, DATA_SIZE);

    current_file_size += DATA_SIZE;
}

static void do_write_middle(void)
{
    long size_in_page = ((lseek(fd, 0, SEEK_END))/DATA_SIZE),
            new_index = (rand() % size_in_page) * DATA_SIZE;

    lseek(fd, new_index, SEEK_SET);

    generate_data();

    must_write(fd, page_data_buffer, DATA_SIZE);

    memcpy(page_data + new_index,
            page_data_buffer, DATA_SIZE);
}

static void do_read_middle(void)
{
    long size_in_page = ((lseek(fd, 0, SEEK_END))/DATA_SIZE),
            new_index = (rand() % size_in_page) * DATA_SIZE;

    lseek(fd, new_index, SEEK_SET);

    must_read(fd, page_data_buffer, DATA_SIZE);

    data_compare(new_index);
}


/*
 * First arg: iterations
 * second arg: chances of adding the page to the end of the file.
 * third arg: maximum size of the file in byte.
 * fourth arg: how many times to report passed test in for loop
 * fifth arg: chances of reading
 */
int main(int argc, char * argv[])
{
    long iterations = 0, i = 0, report = 0;

    unlink(TEST_FILE);

    fd = open(TEST_FILE, O_RDWR | O_CREAT, 0644);

    if (argc != 6)
    {
        printf("Usage: %s iterations chances_of_appending max_file_size "
                "report_batch chances_of_reading\n", argv[0]);

        exit(1);
    }

    iterations = atol(argv[1]);
    add_page_chance = atol(argv[2]);
    max_file_size = atol(argv[3]);
    report = atol(argv[4]);
    read_page_chance = atol(argv[5]);

    page_data = malloc(max_file_size);

    srand(time(NULL));

    for (i = 0; i < iterations; i++)
    {
        int whattodo = decide_what_to_do();

        if (whattodo == ADD_END)
            do_add_to_end();
        else if (whattodo == READ_MIDDLE)
            do_read_middle();
        else if (whattodo == WRITE_MIDDLE)
            do_write_middle();

        if ((i % report) == 0)
        {
            printf("Iter: %ld, Passed test!\n", i);
            fflush(stdout);
        }

    }

    printf("Finally passed test!\n");
    fflush(stdout);


    return 0;
}
