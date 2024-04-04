#include "../../fsutils/libutil.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdbool.h>

static void unlink_dir(const char *base)
{
    long count = 0;
    struct stat st;
    if (lstat(base, &st) < 0)
    {
        edie("failed to stat %s\n", base);
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
            if (unlink(name) < 0)
                edie("failed to unlink %s\n", name);

            count++;
        }

        sufs_fini_string_queue(&names);

        printf("Successfully remove %ld files\n", count);
    }
}

int main(int argc, char *argv[])
{
    int i;

    if (argc != 2)
        die("Usage: %s dir\n", argv[0]);

    unlink_dir(argv[1]);

    return 0;
}
