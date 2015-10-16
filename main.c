#include <stdio.h>
#include <errno.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "dir.h"
#include "../include/dbg.h"

int main(int argc, char *argv[])
{
       int fd, ret_fd;
       char name[258];

       fd = open(argv[1], O_RDONLY);

       if (fd == -1) {
               errno_print();
               return -1;
       }

       while((ret_fd = dir_get_next_filedes(fd, name)) != -1) {
               close(ret_fd);
               printf("%s\n", name);
       }

       close(fd);
       return 0;
}

