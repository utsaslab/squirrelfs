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
#include <pthread.h>

#define TEST_DIR       "/sufs/test/"
//#define TEST_DIR       "sufs/"
#define MAX_NAME_SIZE  64
#define MAX_PATH_SIZE  4096
#define MAX_DIGIT      20

int fd = 0, max_file_num = 0, threads = 0;

void * func(void * arg)
{
    unsigned int seed = time(NULL);
    unsigned long start_idx = (unsigned long) arg;
    int my_current_file = 0;


    while (my_current_file < max_file_num / threads)
    {
        char path[MAX_PATH_SIZE];
        char name[MAX_NAME_SIZE + 1];
        char digit[MAX_DIGIT];
        int dir = rand_r(&seed) % 2;
        int i = 0, ret = 0;

        strcpy(path, TEST_DIR);

        for (i = 0; i < rand_r(&seed) % MAX_NAME_SIZE + 1; i++)
//        for (i = 0; i < MAX_NAME_SIZE; i++)
        {
            name[i] = rand_r(&seed) % 26 + 'a';
        }

        name[i] = '\0';

#if 0
        printf("name: %s\n", name);
#endif

        sprintf(digit, "%ld", (start_idx + my_current_file));
//        sprintf(digit, "%03d", current_file_num);

        strcat(path, digit);
        strcat(path, name);

#if 0
        printf("path: %s\n", path);
#endif

        if (dir)
        {
           ret = mkdir(path, 0777);
        }
        else
        {
           ret = open(path, O_CREAT | O_WRONLY, 0777);
           if (ret >= 0)
               close(ret);
        }

        if (ret < 0)
        {
            fprintf(stderr, "open or mkdir failed!\n");
            exit(1);
        }

        my_current_file++;
    }

    return NULL;
}

int main(int argc, char * argv[])
{
    int i = 0;
    pthread_t * h_threads = NULL;

    if (argc != 3)
    {
        printf("Usage: %s max_file_num threads\n", argv[0]);
        exit(1);
    }

    max_file_num = atol(argv[1]);
    threads = atol(argv[2]);

    h_threads = malloc(sizeof(pthread_t) * threads);


    for (i = 0; i < threads; i++)
    {
        unsigned long idx = max_file_num / threads * i;
        pthread_create(&h_threads[i], NULL, func, (void *) idx);
    }

    for (i = 0; i < threads; i++)
        pthread_join(h_threads[i], NULL);


    printf("Finally passed test!\n");
    fflush(stdout);


    return 0;
}
