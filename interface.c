#include <stdio.h>
#include <errno.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "dir.h"
#include "../include/dbg.h"

FILE *get_next_file(int fd, char *filename, const char *mode)
{
	dir_get_next_filedes(fd, filename);
	return fdopen(fd, mode);
}
