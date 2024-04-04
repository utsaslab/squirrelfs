#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/syscall.h>


#include "libutil.h"

char buf[512];

void cat(int fd)
{
    int n;

    while ((n = read(fd, buf, sizeof(buf))) > 0)
    {
        syscall(SYS_write, 1, buf, n);
    }

    if (n < 0)
    {
        die("cat: read error\n");
    }
}

int main(int argc, char *argv[])
{
    int fd, i;

    if (argc <= 1)
    {
        cat(0);
        return 0;
    }

    for (i = 1; i < argc; i++)
    {
        if ((fd = open(argv[i], 0)) < 0)
        {
            die("cat: cannot open %s\n", argv[i]);
        }
        cat(fd);
        close(fd);
    }

    return 0;
}
