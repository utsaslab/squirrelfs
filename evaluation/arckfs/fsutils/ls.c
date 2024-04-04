#include <sys/stat.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "libutil.h"

char filetype(mode_t m)
{
    if (S_ISDIR(m))
        return 'd';
    if (S_ISREG(m))
        return '-';
    if (S_ISCHR(m))
        return 'c';
    return '?';
}

const char*
fmtname(const char *path)
{
    const char *p;

    // Find first character after last slash.
    for (p = path + strlen(path); p >= path && *p != '/'; p--);
    p++;
    return p;
}

static void printout(struct stat *st, char *path)
{
    printf("%c %-14s %8ld %7zu %3d\n", filetype(st->st_mode), fmtname(path),
            st->st_ino, st->st_size, (unsigned) st->st_nlink);
}

void ls(char *path, int terse)
{
    int fd, first;
    struct stat st;
    struct sufs_string_queue names;
    char namebuf[SUFS_PATH_MAX], prev[SUFS_PATH_MAX];
    char *name = NULL;


    if ((fd = open(path, 0)) < 0)
    {
        fprintf(stderr, "ls: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0)
    {
        fprintf(stderr, "ls: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.st_mode & S_IFMT)
    {
        case S_IFREG:
            printout(&st, path);
            break;

        case S_IFDIR:
            sufs_init_string_queue(&names);

            strcpy(prev, "");

            while (sufs_sys_readdir(fd, prev, namebuf) > 0)
            {
                char tmp[SUFS_PATH_MAX];
                strncpy(prev, namebuf, SUFS_PATH_MAX);

                strncpy(tmp, path, SUFS_PATH_MAX);
                strcat(tmp, "/");
                strcat(tmp, namebuf);

                if (terse)
                {
                    if (strcmp(namebuf, ".") == 0|| strcmp(namebuf, "..") == 0)
                        continue;
                }

                sufs_enqueue_string_queue(&names, tmp);
            }

            sufs_sort_string_queue(&names);

            first = 1;
            while ((name = sufs_dequeue_string_queue(&names)))
            {
                if (terse)
                {
                    if (!first)
                    {
                        printf(",");
                    }

                    printf("%s", name);
                    first = 0;
                    continue;
                }

                if (stat(name, &st) < 0)
                {
                    fprintf(stderr, "ls: cannot stat %s\n", name);
                    continue;
                }

                printout(&st, name);
            }

            break;
    }

    if (terse)
        printf("\n");

    close(fd);
}

int main(int argc, char *argv[])
{
    int i, terse;

    if (strcmp(argv[1], "-t") == 0)
    {
        terse = 1;
        argc--;
        argv++;
    }


    if (argc < 2)
    {
        ls(".", terse);
    }
    else
    {
        for (i = 1; i < argc; i++)
            ls(argv[i], terse);
    }
    return 0;
}
