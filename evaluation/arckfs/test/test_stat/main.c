#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>

int main(void)
{
    int ret = 0;
    struct stat st;

    ret = lstat("/sufs/test/", &st);

    if (ret != 0)
    {
        perror("lstat");
        return 1;
    }

    if (ret == 0)
        printf("/sufs/test st.st_mode is %d\n", st.st_mode);

    ret = lstat("/var/tmp/", &st);

    if (ret != 0)
    {
        perror("lstat");
        return 1;
    }

    if (ret == 0)
        printf("/var/tmp/ st.st_mode is %d\n", st.st_mode);

    return ret;
}

