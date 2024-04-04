#include "../../fsutils/libutil.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdbool.h>

static void rename_dir(const char * src, const char * dst)
{
    long count = 0;

    struct stat src_st;
    struct stat dst_st;

    char buf[SUFS_NAME_MAX], prev[SUFS_NAME_MAX];

    char * name = NULL;
    char * path = NULL;
    struct sufs_string_queue paths, names;

    if (lstat(src, &src_st) < 0)
    {
        edie("failed to stat %s\n", src);
    }

    if (lstat(dst, &dst_st) < 0)
    {
        edie("failed to stat %s\n", src);
    }

    if ( (src_st.st_mode & S_IFMT) != S_IFDIR)
    {
        edie("%s is not a directory\n", src);
    }

    if ( (dst_st.st_mode & S_IFMT) != S_IFDIR)
    {
        edie("%s is not a directory\n", dst);
    }


    sufs_init_string_queue(&paths);
    sufs_init_string_queue(&names);

    // Get all directory entries
    int fd = open(src, O_RDONLY);
    if (fd < 0)
        edie("rm: failed to open %s\n", src);

    strcpy(prev, "");

    while (true)
    {
        char tmp[SUFS_PATH_MAX];

        int r = sufs_sys_readdir(fd, prev, buf);
        strncpy(prev, buf, SUFS_NAME_MAX);

        if (r < 0)
            edie("rm: failed to readdir %s\n", src);
        if (r == 0)
            break;
        if (strcmp(buf, ".") == 0 || strcmp(buf, "..") == 0)
            continue;

        strncpy(tmp, src, SUFS_PATH_MAX);
        strcat(tmp, "/");
        strcat(tmp, buf);

        sufs_enqueue_string_queue(&paths, tmp);
        sufs_enqueue_string_queue(&names, buf);
    }

    close(fd);

    // Delete children
    while ((path = sufs_dequeue_string_queue(&paths)))
    {
        char tmp[SUFS_PATH_MAX];
        name = sufs_dequeue_string_queue(&names);

        strncpy(tmp, dst, SUFS_PATH_MAX);
        strcat(tmp, "/");
        strcat(tmp, name);

        if (rename(path, tmp) < 0)
            edie("failed to rename %s %s\n", name, tmp);

        count++;
    }

    sufs_fini_string_queue(&names);

    printf("Successfully rename %ld files\n", count);
}

int main(int argc, char *argv[])
{
    int i;

    if (argc != 3)
        die("Usage: %s src dst\n", argv[0]);

    rename_dir(argv[1], argv[2]);

    return 0;
}
