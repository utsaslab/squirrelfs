#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/time.h>

#include "../arckfs/fsutils/libutil.h"

const char *MOUNT_POINT = "/sufs";
const char *FS = "arckfs";
const char *OUTPUT_DIR = "output-temp";
const int OP_ITERATIONS = 100000;
const int NAME_LEN = 128;
const int SMALL_BUFFER_SIZE = 1024;
const int MED_BUFFER_SIZE = 1024*16;
const int BIG_BUFFER_SIZE = 1024*64;

long calculate_latency(struct timeval tv_start, struct timeval tv_end) {
    unsigned long start = (unsigned long)tv_start.tv_sec * 1000000 + (unsigned long)tv_start.tv_usec;
    unsigned long end = (unsigned long)tv_end.tv_sec * 1000000 + (unsigned long)tv_end.tv_usec;
    return end - start;
}

int measure_mkdir(char *output_path, int i) {
    int ret;
    FILE* fp;
    char *file_name = "dir";
    char target[64];
    char output_file[NAME_LEN];
    struct timeval tv_start, tv_end;
    unsigned long latency;

    printf("measuring mkdir\n");

    sprintf(output_file, "%s/mkdir/Run%d", output_path, i);
    fp = fopen(output_file, "w");
    if (fp == NULL) {
        perror("fopen");
        return -1;
    }

    for (int j = 0; j < OP_ITERATIONS; j++) {
        sprintf(target, "%s/%s%d", MOUNT_POINT, file_name, j);
        gettimeofday(&tv_start, NULL);
        ret = mkdir(target, 0777);
        gettimeofday(&tv_end, NULL);
        if (ret < 0) {
            perror("mkdir");
            return ret;
        }

        // microseconds
        latency = calculate_latency(tv_start, tv_end);
        fprintf(fp, "%ld,\n", latency);
    }
    
    fclose(fp);

    return 0;
}

int measure_creat(char *output_path, int i) {
    int ret;
    FILE* fp;
    char *file_name = "file";
    char target[64];
    char output_file[NAME_LEN];
    struct timeval tv_start, tv_end;
    unsigned long latency;

    printf("measuring creat\n");

    sprintf(output_file, "%s/creat/Run%d", output_path, i);
    fp = fopen(output_file, "w");
    if (fp == NULL) {
        perror("fopen");
        return -1;
    }

    for (int j = 0; j < OP_ITERATIONS; j++) {
        sprintf(target, "%s/%s%d", MOUNT_POINT, file_name, j);
            
        gettimeofday(&tv_start, NULL);
        ret = open(target, O_CREAT | O_RDWR);
        gettimeofday(&tv_end, NULL);
        if (ret < 0) {
            perror("creat");
            return ret;
        }

        // microseconds
        latency = calculate_latency(tv_start, tv_end);
        fprintf(fp, "%ld,\n", latency);
        close(ret);
    }
    

    fclose(fp);

    return 0;
}

int measure_append_1k(char *output_path, int i) {
    int ret, fd;
    FILE* fp;
    char *file_name = "file";
    char target[64];
    char output_file[NAME_LEN];
    struct timeval tv_start, tv_end;
    unsigned long latency;
    char* write_buffer = malloc(SMALL_BUFFER_SIZE);
    memset(write_buffer, 'a', SMALL_BUFFER_SIZE);

    printf("measuring append 1k\n");

    sprintf(output_file, "%s/append_1k/Run%d", output_path, i);
    fp = fopen(output_file, "w");
    if (fp == NULL) {
        perror("fopen");
        return -1;
    }

    for (int j = 0; j < OP_ITERATIONS; j++) {
        sprintf(target, "%s/%s%d", MOUNT_POINT, file_name, j);
            
        fd = open(target, O_CREAT | O_RDWR);
        if (fd < 0) {
            perror("creat");
            return fd;
        }

        gettimeofday(&tv_start, NULL);
        ret = write(fd, write_buffer, SMALL_BUFFER_SIZE);
        gettimeofday(&tv_end, NULL);
        if (ret < 0) {
            perror("write");
            return ret;
        }
        // microseconds
        latency = calculate_latency(tv_start, tv_end);
        fprintf(fp, "%ld,\n", latency);

        close(fd);
    }
    
    fclose(fp);

    return 0;
}

int measure_append_16k(char *output_path, int i) {
    int ret, fd;
    FILE* fp;
    char *file_name = "file";
    char target[64];
    char output_file[NAME_LEN];
    struct timeval tv_start, tv_end;
    unsigned long latency;
    char* write_buffer = malloc(MED_BUFFER_SIZE);
    memset(write_buffer, 'a', MED_BUFFER_SIZE);

    printf("measuring append 16k\n");

    sprintf(output_file, "%s/append_16k/Run%d", output_path, i);
    fp = fopen(output_file, "w");
    if (fp == NULL) {
        perror("fopen");
        return -1;
    }
    
    for (int j = 0; j < OP_ITERATIONS; j++) {
        sprintf(target, "%s/%s%d", MOUNT_POINT, file_name, j);
            
        fd = open(target, O_CREAT | O_RDWR);
        if (fd < 0) {
            perror("creat");
            return fd;
        }


        gettimeofday(&tv_start, NULL);
        ret = write(fd, write_buffer, MED_BUFFER_SIZE);
        gettimeofday(&tv_end, NULL);
        if (ret < 0) {
            perror("write");
            return ret;
        }
        // microseconds
        latency = calculate_latency(tv_start, tv_end);
        fprintf(fp, "%ld,\n", latency);

        close(fd);
    }
    
    fclose(fp);

    return 0;
}

int measure_read_1k(char *output_path, int i) {
    int ret, fd;
    FILE* fp;
    char *file_name = "file";
    char target[64];
    char output_file[NAME_LEN];
    struct timeval tv_start, tv_end;
    unsigned long latency;
    char* buffer = malloc(SMALL_BUFFER_SIZE);
    memset(buffer, 'a', SMALL_BUFFER_SIZE);

    printf("measuring read 1k\n");

    sprintf(output_file, "%s/read_1k/Run%d", output_path, i);
    fp = fopen(output_file, "w");
    if (fp == NULL) {
        perror("fopen");
        return -1;
    }

    for (int j = 0; j < OP_ITERATIONS; j++) {
        sprintf(target, "%s/%s%d", MOUNT_POINT, file_name, j);
            
        fd = open(target, O_CREAT | O_RDWR);
        if (fd < 0) {
            perror("creat");
            return fd;
        }

        ret = write(fd, buffer, SMALL_BUFFER_SIZE);
        if (ret < 0) {
            perror("write");
            return ret;
        }

        close(fd);
    }

    for (int j = 0; j < OP_ITERATIONS; j++) {
        sprintf(target, "%s/%s%d", MOUNT_POINT, file_name, j);
            
        fd = open(target, O_CREAT | O_RDWR);
        if (fd < 0) {
            perror("creat");
            return fd;
        }


        gettimeofday(&tv_start, NULL);
        ret = read(fd, buffer, SMALL_BUFFER_SIZE);
        gettimeofday(&tv_end, NULL);
        if (ret < 0) {
            perror("write");
            return ret;
        }
        // microseconds
        latency = calculate_latency(tv_start, tv_end);
        fprintf(fp, "%ld,\n", latency);

        close(fd);
    }
    
    fclose(fp);

    return 0;
}

int measure_read_16k(char *output_path, int i) {
    int ret, fd;
    FILE* fp;
    char *file_name = "file";
    char target[64];
    char output_file[NAME_LEN];
    struct timeval tv_start, tv_end;
    unsigned long latency;
    char* buffer = malloc(MED_BUFFER_SIZE);
    memset(buffer, 'a', MED_BUFFER_SIZE);

    printf("measuring read 16k\n");

    sprintf(output_file, "%s/read_16k/Run%d", output_path, i);
    fp = fopen(output_file, "w");
    if (fp == NULL) {
        perror("fopen");
        return -1;
    }

    for (int j = 0; j < OP_ITERATIONS; j++) {
        sprintf(target, "%s/%s%d", MOUNT_POINT, file_name, j);
            
        fd = open(target, O_CREAT | O_RDWR);
        if (fd < 0) {
            perror("creat");
            return fd;
        }

        ret = write(fd, buffer, MED_BUFFER_SIZE);
        if (ret < 0) {
            perror("write");
            return ret;
        }

        close(fd);
    }

    for (int j = 0; j < OP_ITERATIONS; j++) {
        sprintf(target, "%s/%s%d", MOUNT_POINT, file_name, j);
            
        fd = open(target, O_CREAT | O_RDWR);
        if (fd < 0) {
            perror("creat");
            return fd;
        }


        gettimeofday(&tv_start, NULL);
        ret = read(fd, buffer, MED_BUFFER_SIZE);
        gettimeofday(&tv_end, NULL);
        if (ret < 0) {
            perror("write");
            return ret;
        }
        // microseconds
        latency = calculate_latency(tv_start, tv_end);
        fprintf(fp, "%ld,\n", latency);

        close(fd);
    }
    
    fclose(fp);

    return 0;
}

int measure_unlink(char *output_path, int i) {
    int ret, fd;
    FILE* fp;
    char *file_name = "file";
    char target[64];
    char output_file[NAME_LEN];
    struct timeval tv_start, tv_end;
    unsigned long latency;
    char* write_buffer = malloc(MED_BUFFER_SIZE);
    memset(write_buffer, 'a', MED_BUFFER_SIZE);

    printf("measuring unlink\n");

    sprintf(output_file, "%s/unlink/Run%d", output_path, i);
    fp = fopen(output_file, "w");
    if (fp == NULL) {
        perror("fopen");
        return -1;
    }

    for (int j = 0; j < OP_ITERATIONS; j++) {
        sprintf(target, "%s/%s%d", MOUNT_POINT, file_name, j);
            
        fd = open(target, O_CREAT | O_RDWR);
        if (fd < 0) {
            perror("creat");
            return fd;
        }

        ret = write(fd, write_buffer, MED_BUFFER_SIZE);
        if (ret < 0) {
            perror("write");
            return ret;
        }

        close(fd);
    }

    for (int j = 0; j < OP_ITERATIONS; j++) {
        sprintf(target, "%s/%s%d", MOUNT_POINT, file_name, j);
        
        gettimeofday(&tv_start, NULL);
        ret = unlink(target);
        gettimeofday(&tv_end, NULL);
        if (ret < 0) {
            perror("unlink");
            return ret;
        }

        // microseconds
        latency = calculate_latency(tv_start, tv_end);
        fprintf(fp, "%ld,\n", latency);
    }

    fclose(fp);

    return 0;
}

int measure_rename(char *output_path, int i) {
    int ret;
    FILE* fp;
    char *file_name = "dir";
    char target[64], source[64];
    char output_file[NAME_LEN];
    struct timeval tv_start, tv_end;
    unsigned long latency;

    printf("measuring rename\n");

    sprintf(output_file, "%s/rename/Run%d", output_path, i);
    fp = fopen(output_file, "w");
    if (fp == NULL) {
        perror("fopen");
        return -1;
    }

    sprintf(target, "%s/src_parent/", MOUNT_POINT);
    ret = mkdir(target, 0777);
    if (ret < 0) {
        perror("mkdir");
        return ret;
    }
    sprintf(target, "%s/dst_parent/", MOUNT_POINT);
    ret = mkdir(target, 0777);
    if (ret < 0) {
        perror("mkdir");
        return ret;
    }

    printf("making directories\n");
    for (int j = 0; j < OP_ITERATIONS; j++) {
        sprintf(target, "%s/src_parent/%s%d", MOUNT_POINT, file_name, j);
        gettimeofday(&tv_start, NULL);
        ret = mkdir(target, 0777);
        gettimeofday(&tv_end, NULL);
        if (ret < 0) {
            perror("mkdir");
            return ret;
        }

        // microseconds
        latency = calculate_latency(tv_start, tv_end);
        // fprintf(fp, "%ld,\n", latency); //?
    }

    printf("renaming\n");
    for (int j = 0; j < OP_ITERATIONS; j++) {
        sprintf(source, "%s/src_parent/%s%d", MOUNT_POINT, file_name, j);
        sprintf(target, "%s/dst_parent/%s%d", MOUNT_POINT, file_name, j);

        gettimeofday(&tv_start, NULL);
        ret = rename(source, target);
        gettimeofday(&tv_end, NULL);
        if (ret < 0) {
            perror("rename");
            return ret;
        }

        // microseconds
        latency = calculate_latency(tv_start, tv_end);
        fprintf(fp, "%ld,\n", latency);
    }

    fclose(fp);

    return 0;
}

int main(int argc, void *argv[]) {
    int ret, iteration;
    char *syscall;
    char output_path[NAME_LEN];
    memset(output_path, 0, NAME_LEN);

    if (argc < 3) {
        printf("Usage: syscall_latency syscall iteration\n");
        return 1;
    }
    syscall = (char*)argv[1];
    iteration = atoi((char*)argv[2]);

    sprintf(output_path, "%s/%s/syscall_latency", OUTPUT_DIR, FS);

    if (strcmp(syscall, "mkdir") == 0) {
        ret = measure_mkdir(output_path, iteration);
        if (ret < 0) {
            return ret;
        }
    } else if (strcmp(syscall, "creat") == 0) {
        ret = measure_creat(output_path, iteration);
        if (ret < 0) {
            return ret;
        }
    } else if (strcmp(syscall, "append_1k") == 0) {
        ret = measure_append_1k(output_path, iteration);
        if (ret < 0) {
            return ret;
        }
    } else if (strcmp(syscall, "append_16k") == 0) {
        ret = measure_append_16k(output_path, iteration);
        if (ret < 0) {
            return ret;
        }
    } else if (strcmp(syscall, "read_1k") == 0) {
        ret = measure_read_1k(output_path, iteration);
        if (ret < 0) {
            return ret;
        }
    } else if (strcmp(syscall, "read_16k") == 0) {
        ret = measure_read_16k(output_path, iteration);
        if (ret < 0) {
            return ret;
        }
    } else if (strcmp(syscall, "unlink") == 0) {
        ret = measure_unlink(output_path, iteration);
        if (ret < 0) {
            return ret;
        }
    } else if (strcmp(syscall, "rename") == 0) {
        ret = measure_rename(output_path, iteration);
        if (ret < 0) {
            return ret;
        }
    } else {
        printf("Unrecognized system call\n");
        return -1;
    }

    return 0;
}