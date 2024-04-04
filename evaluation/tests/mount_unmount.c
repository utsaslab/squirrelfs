#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/time.h>

const char *MOUNT_POINT = "/mnt/pmem";

int mountfs(char* mount_type) {
    // char *mount_opts, *command;
    // if (strcmp(mount_type, "ext4") == 0) {
    //     mount_opts = "dax";
    //     command = "yes | mkfs.ext4 /dev/pmem0";
    //     system(command);
    // } else {
    //     mount_opts = "init";
    // }
    int ret = mount("/dev/pmem0", MOUNT_POINT, mount_type, 0, NULL);
    if (ret < 0) {
        perror("mount");
        return ret;
    }
    return 0;
}

int unmountfs() {
    int ret = umount(MOUNT_POINT);
    if (ret < 0) {
        perror("umount");
        return ret;
    }
}

int main(void) {
    mountfs("hayleyfs");
}