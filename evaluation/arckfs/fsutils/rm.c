#include "libutil.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdbool.h>

static void rmtree(const char *base)
{
    struct stat st;
    if (lstat(base, &st) < 0)
    {
        edie("rm: failed to stat %s\n", base);
    }

    if ((st.st_mode & S_IFMT) == S_IFDIR)
    {
        char buf[SUFS_NAME_MAX], prev[SUFS_NAME_MAX];

        char * name = NULL;
        struct sufs_string_queue names;

        sufs_init_string_queue(&names);

        // Get all directory entries
        int fd = open(base, O_RDONLY);
        if (fd < 0)
            edie("rm: failed to open %s\n", base);

        strcpy(prev, "");

        while (true)
        {
            char tmp[SUFS_PATH_MAX];

            int r = sufs_sys_readdir(fd, prev, buf);
            strncpy(prev, buf, SUFS_NAME_MAX);

            if (r < 0)
                edie("rm: failed to readdir %s\n", base);
            if (r == 0)
                break;
            if (strcmp(buf, ".") == 0 || strcmp(buf, "..") == 0)
                continue;

            strncpy(tmp, base, SUFS_PATH_MAX);
            strcat(tmp, "/");
            strcat(tmp, buf);

            sufs_enqueue_string_queue(&names, tmp);
        }

        close(fd);
        // Delete children
        while ((name = sufs_dequeue_string_queue(&names)))
        {
            rmtree(name);
        }

        sufs_fini_string_queue(&names);
    }

    if (unlink(base) < 0)
        edie("rm: failed to unlink %s\n", base);
}

int main(int argc, char *argv[])
{
    int i;

    if (argc < 2)
        die("Usage: rm [-r] files...\n");

    bool recursive = false;
    if (strcmp(argv[1], "-r") == 0)
    {
        recursive = true;
        argc--;
        argv++;
    }

    for (i = 1; i < argc; i++)
    {
        if (recursive)
        {
            rmtree(argv[i]);
        }
        else if (unlink(argv[i]) < 0)
        {
            die("rm: %s failed to delete\n", argv[i]);
        }
    }

    return 0;
}
