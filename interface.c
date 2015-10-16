#include <stdio.h>
#include <errno.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "dir.h"
#include "../include/dbg.h"

FILE *get_next_file(int fd, char *filename, const char *mode)
{
	int next_fd;
	next_fd = dir_get_next_filedes(fd, filename);

	/* next_fd is now part of the FILE, no need to care about it */
	return fdopen(next_fd, mode);
}
