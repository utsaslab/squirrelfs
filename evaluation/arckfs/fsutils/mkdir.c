#include <sys/stat.h>
#include <errno.h>

#include "libutil.h"

int
main(int argc, char *argv[])
{
    int i;

    if (argc < 2)
    {
        die("usage: mkdir files...\n");
    }

    for (i = 1; i < argc; i++)
    {
        if (mkdir(argv[i], 0777) < 0)
            die("mkdir: %s failed to create with errno: %d\n", argv[i], errno);
    }

    return 0;
}
