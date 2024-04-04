#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "libutil.h"

int
main(int argc, char *argv[])
{
  int i, fd;

  fd = open(argv[1], O_CREAT | O_TRUNC | O_WRONLY, 0777);

  if (fd < 0)
  {
      die("open file :%s failed!\n", argv[1]);
  }

  close(fd);

  return 0;
}
