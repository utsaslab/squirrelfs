#include <dirent.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>  /* For BUFSIZ.  */
#include <sys/param.h>  /* For MIN and MAX.  */
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "../include/libfs_config.h"
#include "compiler.h"
#include "syscall.h"

struct __dirstream
{
    int fd;         /* File descriptor.  */

    pthread_spinlock_t lock; /* Mutex lock for this structure.  */

    size_t allocation;      /* Space allocated for the block.  */
    size_t size;        /* Total valid data in the block.  */
    size_t offset;      /* Current offset into the block.  */

    off_t filepos;      /* Position of next entry to read.  */

    int errcode;        /* Delayed error code.  */

    /* Directory block.  We must make sure that this block starts
       at an address that is aligned adequately enough to store
       dirent entries.  Using the alignment of "void *" is not
       sufficient because dirents on 32-bit platforms can require
       64-bit alignment.  We use "long double" here to be consistent
       with what malloc uses.  */
    char data[0] __attribute__ ((aligned (__alignof__ (long double))));
};

typedef struct __dirstream DIR;


/* The st_blksize value of the directory is used as a hint for the
   size of the buffer which receives struct dirent values from the
   kernel.  st_blksize is limited to MAX_DIR_BUFFER_SIZE, in case the
   file system provides a bogus value.  */
#define MAX_DIR_BUFFER_SIZE 1048576U

enum {
    opendir_oflags = O_RDONLY
};

static DIR *
__alloc_dir (int fd, bool close_fd, int flags)
{
    size_t allocation = MAX_DIR_BUFFER_SIZE;

    DIR *dirp = (DIR *) malloc (sizeof (DIR) + allocation);

    dirp->fd = fd;

    pthread_spin_init (&dirp->lock, PTHREAD_PROCESS_PRIVATE);
    dirp->allocation = allocation;
    dirp->size = 0;
    dirp->offset = 0;
    dirp->filepos = 0;
    dirp->errcode = 0;

    return dirp;
}

static DIR *
opendir_tail (int fd)
{
    if (unlikely (fd < 0))
        return NULL;

    return __alloc_dir (fd, true, 0);
}


/* Open a directory stream on NAME.  */
DIR *
sufs_opendir (const char *name)
{
    return opendir_tail (open(name, opendir_oflags));
}

struct dirent * sufs_glibc_readdir (DIR *dirp)
{
    struct dirent *dp;
    int saved_errno = errno;

    pthread_spin_lock(&dirp->lock);

    do
    {
        size_t reclen;

        if (dirp->offset >= dirp->size)
        {
			/* We've emptied out our buffer.  Refill it.  */

			size_t maxread = dirp->allocation;
			ssize_t bytes;

			bytes = getdents64(dirp->fd, dirp->data, maxread);
			if (bytes <= 0)
			{
				/* On some systems getdents fails with ENOENT when the
				open directory has been rmdir'd already.  POSIX.1
				requires that we treat this condition like normal EOF.  */
				if (bytes < 0 && errno == ENOENT)
				{
					bytes = 0;
				}

				/* Don't modifiy errno when reaching EOF.  */
				if (bytes == 0)
				{
				    errno = saved_errno;
				}
				dp = NULL;
				break;
			}

			dirp->size = (size_t) bytes;

			/* Reset the offset into the buffer.  */
			dirp->offset = 0;
		 }

      dp = (struct dirent *) &dirp->data[dirp->offset];

      reclen = dp->d_reclen;

      dirp->offset += reclen;

      dirp->filepos = dp->d_off;

      /* Skip deleted files.  */
    } while (dp->d_ino == 0);

  pthread_spin_unlock(&dirp->lock);

  return dp;
}


/* Read a directory entry from DIRP.  */
int
sufs_glibc_readdir_r (DIR *dirp, struct dirent *entry, struct dirent **result)
{
    struct dirent *dp;
    size_t reclen;
    const int saved_errno = errno;
    int ret;

    pthread_spin_lock(&dirp->lock);

    do
    {
        if (dirp->offset >= dirp->size)
        {
            /* We've emptied out our buffer.  Refill it.  */

            size_t maxread;
            ssize_t bytes;

            maxread = dirp->allocation;

            bytes = getdents64(dirp->fd, dirp->data, maxread);
            if (bytes <= 0)
            {
                /* On some systems getdents fails with ENOENT when the
                   open directory has been rmdir'd already.  POSIX.1
                   requires that we treat this condition like normal EOF.  */
                if (bytes < 0 && errno == ENOENT)
                {
                    bytes = 0;
                    errno = saved_errno;
                }
                if (bytes < 0)
                    dirp->errcode = errno;

                dp = NULL;
                break;
            }

            dirp->size = (size_t) bytes;

            /* Reset the offset into the buffer.  */
            dirp->offset = 0;
        }

        dp = (struct dirent *) &dirp->data[dirp->offset];

        reclen = dp->d_reclen;

        dirp->offset += reclen;

        dirp->filepos = dp->d_off;

    /* Skip deleted and ignored files.  */
    }
    while (dp->d_ino == 0);

    if (dp != NULL)
    {
        *result = memcpy (entry, dp, reclen);
        entry->d_reclen = reclen;
        ret = 0;
    }
    else
    {
        *result = NULL;
        ret = dirp->errcode;
    }

    pthread_spin_unlock(&dirp->lock);

    return ret;
}



int
sufs_closedir (DIR *dirp)
{
  int fd;

  if (dirp == NULL)
  {
      errno = EINVAL;
      return -1;
  }

  /* We do not try to synchronize access here.  If some other thread
     still uses this handle it is a big mistake and that thread
     deserves all the bad data it gets.  */

  fd = dirp->fd;

  pthread_spin_destroy (&(dirp->lock));

  free ((void *) dirp);

  return close(fd);
}
