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
  int i, fd, size;

  if (argc != 3)
  {
      fprintf(stderr, "Usage :%s file size\n", argv[0]);
      exit(1);
  }


  fd = open(argv[1], O_CREAT | O_WRONLY, 0777);

  if (fd < 0)
  {
      die("open file :%s failed!\n", argv[1]);
  }


  size = atol(argv[2]);

  if (ftruncate(fd, size) != 0)
  {
      die("ftruncate failed!\n");
  }

  close(fd);

  return 0;
}
