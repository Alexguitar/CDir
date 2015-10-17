#ifndef _DIR_H_
#define _DIR_H_

/* interface.c functions */
FILE *get_next_file(int fd, char *filename, const char *mode);

/* core.c functions */
int dir_get_next_filedes(int fd, char *filename);

#endif
