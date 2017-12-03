#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define FUSE_USE_VERSION 26
#include <fuse.h>

#include "storage.h"
#include "hints/pages.h"

typedef struct file_data {
    const char* path;
    int         mode;
    const char* data;
} file_data;

static file_data file_table[] = {
    {"/", 0040755, NULL},
    {"/hello.txt", 0100644, "hello\n"},
    {0, 0, 0},
};

int ft_size(const file_data* ft) {
	int c = 0;
	file_data d = ft[0];
	while (d.path != NULL) {
		d = ft[++c];
	}
	return c;
}

void
storage_init(const char* path)
{
	//	pages_init(path);
}

static int
streq(const char* aa, const char* bb)
{
    return strcmp(aa, bb) == 0;
}

static file_data*
get_file_data(const char* path) {
    for (int ii = 0; 1; ++ii) {
        file_data row = file_table[ii];

        if (file_table[ii].path == 0) {
            break;
        }

        if (streq(path, file_table[ii].path)) {
            return &(file_table[ii]);
        }
    }

    return 0;
}

int
get_stat(const char* path, struct stat* st)
{
    file_data* dat = get_file_data(path);
    if (!dat) {
        return -1;
    }

    memset(st, 0, sizeof(struct stat));
    st->st_uid  = getuid();
    st->st_gid = getgid();
    st->st_mode = dat->mode;
    if (dat->data) {
        st->st_size = strlen(dat->data);
    }
    else {
        st->st_size = 0;
    }
    return 0;
}

const char*
get_data(const char* path)
{
    file_data* dat = get_file_data(path);
    if (!dat) {
        return 0;
    }

    return dat->data;
}

int check_bit(int i, int b) {
	return (i & (1 << b)) >> b;
}

int check_mode(const file_data* f, int mode) {
	// NOTE: we only check owner perms
	int flags = (f->mode & 0b111000000) >> 6;
	return (flags & mode) != 0;
}

int get_access(const char* path, int mode) {
	file_data* dat = get_file_data(path);
	if (!dat) {
		return -ENOENT;
	}

	if (mode == F_OK) {
		return 0;
	}

	if (!check_mode(dat, mode)) {
		return -EACCES;
	}

	return 0;
}

int get_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                off_t offset, struct fuse_file_info* fi) {
	(void) offset;
	(void) fi;

	if (strncmp(path, "/", 1) != 0) {
		return -ENOENT;
	}

	struct stat st;
	if (get_stat(path, &st) != 0) {
		return -ENOENT;
	}
	
	filler(buf, ".", &st, 0);
	filler(buf, "..", &st, 0);
	
	int ft_sz = ft_size(file_table);
	unsigned long path_len = strlen(path);

	for (int i = 0; i < ft_sz; i++) {
		file_data fd = file_table[i];
		if (strncmp(fd.path, path, path_len) == 0 && strlen(fd.path) != path_len) {
			if(get_stat(fd.path, &st) != 0) {
				return -ENOENT;
			}
			filler(buf, fd.path + path_len, NULL, 0);
		}
	}
	
	return 0;
}
