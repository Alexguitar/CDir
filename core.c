#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "../include/dbg.h"

//TODO: improve portability, especially with struct dirent. Also strdup()

/* a light-weight replacement for struct dirent */
struct dir_entry {
	long loc; /* position within the directory; result of telldir() */
	char *d_name;
	unsigned char d_type;
};

struct dir_node {
	int dir_fd;
	int lenght;
	int current;
	struct dir_entry *file;
};

static int qsort_cmp_alpha(const void *one, const void *two)
{
	return strcasecmp( ((struct dir_entry *) one)->d_name, \
		((struct dir_entry *) two)->d_name);
}

/* filter out all directories or links and put them at the beginning of list */
static int prioritize_directories(struct dir_entry *file, int size)
{
	struct dir_entry tmp;
	int i, first, next;
	if (file == NULL) {
		log_err("Invalid list");
		return -1;
	}

	first  = 0;
	for (i = 0; i < size; i++) {
		if (file[i].d_type == DT_DIR
		 || file[i].d_type == DT_LNK) {
			if (first == i) {
				first++;
				continue;
			}

			/* swap i into first and save it in tmp */
			tmp = file[first];
			file[first] = file[i];
			next = i;
			/* starting with i-1 push everything forward */
			while (next - 1 > first) {
				file[next] = file[next-1];
				next--;
			}
			file[first+1] = tmp;
			first++;
		}
	}


	return 0;
}

/* sort the given list alphabetically and according to type */
static int sort_file_list(struct dir_entry *list, int size)
{
	if (list == NULL) {
		log_err("Invalid list");
		return -1;
	}

	qsort(list, size, sizeof(struct dir_entry), &qsort_cmp_alpha);
	prioritize_directories(list, size);
	return 0;
}

/* reads the contents of the directory; saves the position of and information
 * about the file in an unsorted list
 */
static struct dir_entry *create_dir_file_list(int fd, int *size)
{
	DIR *dirp;
	int i, tmp_fd;
	int lenght = 32;

	struct dir_entry *list, *tmp;
	struct dirent *entry;


	if (fd == -1) {
		log_err("Illegal file descriptor");
		return NULL;
	}

	/* copy the file descriptor for internal use of fdopendir() */
	if ((tmp_fd = dup(fd)) == -1) {
		errno_print();
		return NULL;
	}

	if ((dirp = fdopendir(tmp_fd)) == NULL) {
		errno_print();
		return NULL;
	}

	/* allocate an initial size */
	list = malloc(sizeof(struct dir_entry) * lenght);
	if (list == NULL) {
		log_err("Failed to allocate list");
		closedir(dirp);
		return NULL;
	}

	errno = 0;
	for (i = 0;; i++) {
		/* expand list if required */
		if (i == lenght) {
			lenght *= 2;
			tmp = realloc(list, sizeof(struct dir_entry) * lenght);
			if (tmp == NULL) {
				log_err("Failed to resize list");
				goto error;
			}
			list = tmp;
		}

		/* find the beginning of the file in the directory */
		list[i].loc = telldir(dirp);

		if ((entry = readdir(dirp)) == NULL && errno != 0) {
			errno_print();
			goto error;
		}
		else if (entry == NULL)
			break;
//TODO: check for support of strdup
//	and support for the d_type field in struct dentry

		list[i].d_name = strdup(entry->d_name);
		list[i].d_type = entry->d_type;

	}

	if (i == 0) {
		goto error;
	}

	/* shrink list accordingly */
	tmp = realloc(list, sizeof(struct dir_entry) * i);
	if (tmp == NULL) {
		log_err("Failed to trim list; doing nothing");
	}
	else list = tmp;

	*size = i;
	closedir(dirp);
	return list;

error:
	closedir(dirp);
	free(list);
	return NULL;
}

/* frees and sets the name field of every file in the list to NULL */
int scrub_dir_file_list(struct dir_entry *list, int size)
{
	if (list == NULL) {
		log_err("Invalid list");
		return -1;
	}

	while (size != 0) {
		if (list[size-1].d_name != NULL) {
			free(list[size-1].d_name);
			list[size-1].d_name = NULL;
		}
		size--;
	}

	return 0;
}

/* decide whether to ignore the file
 * mainly for internal use of dir_get_next_filedes()
 */
static int ignore_file(const char *filename)
{
	/* must ignore '.' and '..', but others are optional */
	if (filename[0] == '.')
		return 1;
	else return 0;
}

/* on each call returns an open file descriptor for the next file within the
 * directory described by fd. Keeps processing the same directory until a change
 * to fd occurs.
 * Returns -1 when finished scanning or on error
 * Not thread safe
 */

int dir_get_next_filedes(int fd, char *filename)
{
	static struct dir_node *tree = NULL;
	struct dir_node *tmp;

	/* max_depth assumes a default maximum branch lenght; is expanded
 	 * whenever needed in main_loop
 	 */
	static int max_depth;
	static int depth;

	/* keeps track of the currently processed directory */
	static int current_fd = -1;

	int current;
	int ret_fd;

	static DIR *dirp;
	struct dirent *entry;

	if (fd == -1) {
		log_err("Illegal file descriptor");
		return -1;
	}

	/* init for new fd */
	if (fd != current_fd) {
		if (tree != NULL) {
			free(tree);
		}
		tree = malloc(sizeof(struct dir_node) * 16);
		if (tree == NULL) {
			log_err("Failed to allocate tree");
			return -1;
		}
		depth = 0;
		max_depth = 16;
		current_fd = fd;

		/* setup for root node */
		tree[0].dir_fd = fd;
		tree[0].file = create_dir_file_list(tree[0].dir_fd, &tree[0].lenght);
		if (tree[0].file == NULL) {
			/* an error has already been printed */
			return -1;
		}
		tree[0].current = 0;

		sort_file_list(tree[0].file, tree[0].lenght);

		/* delete all but the first file entry from the file list
		 * Anything that has been deleted can be reread later thanks to
		 * the loc field from the dir_entry struct
		 */
		if (scrub_dir_file_list(tree[0].file + 1, tree[0].lenght - 1) == -1) {
			/* an error has already been printed */
			return -1;
		}

		/* open directory */
		dirp = fdopendir(fd);
	}



	/* start processing entires from the file list */
main_loop:
	while (tree[depth].current < tree[depth].lenght) {
		current = tree[depth].current;

		/* if it's not present in memory, read it */
		if (tree[depth].file[current].d_name == NULL) {
			seekdir(dirp, tree[depth].file[current].loc);
			errno = 0;
			entry = readdir(dirp);
			if (errno != 0) {
				log_err("");
				return -1;
			}
			tree[depth].file[current].d_name = entry->d_name;
		}
		else {
			entry = NULL;
		}

		/* decide whether to skip the file. Important when the file
 		 * is the . or .. directory
 		 */
		if (ignore_file(tree[depth].file[current].d_name)) {
			/* avoid freeing the static allocation of entry */
			if (entry == NULL) {
				free(tree[depth].file[current].d_name);
			}

			tree[depth].file[current].d_name = NULL;

			tree[depth].current++;
			continue;
		}

#if 0
		//TODO: handle links and FIFOs correctly
		if (tree[depth].file[current].d_type == DT_LNK
		 || tree[depth].file[current].d_type == DT_FIFO) {
			/* avoid freeing the static allocation of entry */
			if (entry == NULL) {
				free(tree[depth].file[current].d_name);
			}

			tree[depth].file[current].d_name = NULL;

			tree[depth].current++;
			continue;
		}
#endif

		/* handle directories */
		if (tree[depth].file[current].d_type == DT_DIR) {
			tree[depth].current++;

			/** create new higher branch **/
			depth++;


			/* expand tree if needed */
			if (depth > max_depth - 1) {
				max_depth = 2 * max_depth;
				tmp = realloc(tree, sizeof(struct dir_node) * \
					max_depth);
				if (tmp == NULL) {
					log_err("Failed to resize tree");
					return -1;
				}
				tree = tmp;
			}


			/* open a file descriptor for the directory */
			tree[depth].dir_fd = openat(tree[depth-1].dir_fd, \
				tree[depth-1].file[current].d_name, O_RDONLY | O_NONBLOCK);

			if (tree[depth].dir_fd == -1) {
				errno_print();
				return -1;
			}


			/* free the directory entry from the lower branch; won't
 			 * be used anymore, but avoid freeing the static
 			 * allocation of entry
			 */
			if (entry == NULL) {

				free(tree[depth-1].file[current].d_name);
			}
			tree[depth-1].file[current].d_name = NULL;


			/* setup for the new node */
			tree[depth].file = create_dir_file_list(tree[depth].dir_fd, \
				&tree[depth].lenght);
			if (tree[depth].file == NULL) {
				/* error has already been printed */
				return -1;
			}

			sort_file_list(tree[depth].file, tree[depth].lenght);

			tree[depth].current = 0;


			/* delete all but the first file entry from the file
			 * list. Anything that has been deleted can be reread
			 * later thanks to the loc field from the dir_entry
			 * struct
 			 */
			if (scrub_dir_file_list(tree[depth].file + 1,
				tree[depth].lenght - 1) == -1) {
				/* an error has already been printed */
				return -1;
			}

			closedir(dirp);
			dirp = fdopendir(tree[depth].dir_fd);

			/* start processing it */
			continue;
		}

		/* TODO: handle DT_UNKNOWN */
		/* handle other files; assumed to be regular */
		else if (tree[depth].file[current].d_type == DT_REG) {

			ret_fd = openat(tree[depth].dir_fd, \
				tree[depth].file[current].d_name, O_RDONLY);
			if (ret_fd == -1) {
				errno_print();
				return -1;
			}

			strcpy(filename, tree[depth].file[current].d_name);
			/* avoid freeing the static allocation of entry */
			if (entry == NULL) {
				free(tree[depth].file[current].d_name);
			}
			else {
				tree[depth].file[current].d_name = NULL;
			}

			tree[depth].current++;
			return ret_fd;
		}

	}

	/* end of branch */
	depth--;

	/* if there are branches left */
	if (depth != -1) {
		/* reopen the file descriptor for the previous directory */
		tree[depth].dir_fd = openat(tree[depth+1].dir_fd, "..", O_RDONLY);

		/* closedir also closes the file descriptor (if any) */
		closedir(dirp);

		/* cleanup */
		scrub_dir_file_list(tree[depth+1].file, tree[depth+1].lenght);
		free(tree[depth+1].file);

		/* restore the directory stream */
		dirp = fdopendir(tree[depth].dir_fd);
		goto main_loop;
	}
	else {
		scrub_dir_file_list(tree[0].file, tree[0].lenght);
		free(tree[0].file);
		free(tree);
		closedir(dirp);
		return -1;
	}


}
