#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>

#include "libutil.h"

int
main(int argc, char *argv[])
{
  struct stat stsrc, stdst;

  if (argc != 3)
  {
    die("Usage: mv src dst\n");
  }

  if (rename(argv[1], argv[2]) < 0)
  {
    die("mv: error renaming %s to %s\n", argv[1], argv[2]);
  }

  /*
   * If both paths point to the same inode, the rename() system call returns
   * success with no further action. In this case, POSIX allows 'mv' to delete
   * the source path to the file, although this is not mandatory.
   */
  if (!stat(argv[1], &stsrc) && !stat(argv[2], &stdst))
  {
    if (stsrc.st_ino == stdst.st_ino)
    {
      unlink(argv[1]);
    }
  }

  return 0;
}
