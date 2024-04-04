#ifndef LIBUTIL_H_
#define LIBUTIL_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SUFS_DEV_PATH "/dev/supremefs"
#define SUFS_CMD_DEBUG_INIT    0x2001

#define die(...) do { \
    fprintf(stderr, __VA_ARGS__); \
    exit(1); \
} while (0)

#define edie(...) do { \
    fprintf(stderr, __VA_ARGS__); \
    exit(1); \
} while (0)

#define SUFS_NAME_MAX               255
#define SUFS_PATH_MAX              4096

//#define SUFS_STRING_QUEUE_SIZE    4096
#define SUFS_STRING_QUEUE_SIZE    1048576

struct sufs_string_queue
{
        int tail, head, size;
        char (*buf)[SUFS_PATH_MAX];
};

static void sufs_init_string_queue(struct sufs_string_queue *queue)
{
    queue->tail = 0;
    queue->head = 0;
    queue->size = SUFS_STRING_QUEUE_SIZE;
    queue->buf = calloc(SUFS_STRING_QUEUE_SIZE, SUFS_PATH_MAX);
}

static void sufs_enqueue_string_queue(struct sufs_string_queue *queue,
        char *path)
{
    if (queue->tail >= queue->size)
        die("queue->tail: %d, queue->size: %d\n", queue->tail, queue->size);

    strncpy(queue->buf[queue->tail], path, SUFS_PATH_MAX);

    queue->tail++;
}

/* return NULL for end of the queue */

/*
 * There is no need to separate the iterator and the data structure for our
 * workload
 */
static char* sufs_dequeue_string_queue(struct sufs_string_queue *queue)
{
    int head = 0;
    if (queue->head >= queue->tail)
        return NULL;

    head = queue->head;

    queue->head++;

    return queue->buf[head];
}

static void sufs_fini_string_queue(struct sufs_string_queue *queue)
{
    queue->tail = queue->head = queue->size = 0;

    free(queue->buf);
}

int string_cmp(const void *a, const void *b)
{
    return strcmp((char*) a, (char*) b);
}

static void sufs_sort_string_queue(struct sufs_string_queue *queue)
{
    qsort(queue->buf, queue->tail, SUFS_PATH_MAX, string_cmp);
}


int sufs_sys_readdir(int fd, char * prev_name, char * this_name);


#endif /* LIBUTIL_H_ */
