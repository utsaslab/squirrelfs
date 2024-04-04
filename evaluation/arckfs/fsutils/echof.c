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


  for(i = 2; i < argc; i++)
  {
      if (write(fd, argv[i], strlen(argv[i])) < 0)
      {
          die("echof: write failed!\n");
      }

      if (i + 1 < argc)
      {
          if (write(fd, " ", 1) < 0)
          {
              die("echof: write failed!\n");
          }
      }
  }


  if (write(fd, "\n", 1) < 0)
  {
      die("echof: write failed!\n");
  }

  close(fd);

  return 0;
}
