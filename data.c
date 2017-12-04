#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>

#include "data.h"

const int NUFS_SIZE = 1024 * 1024;
const int PAGE_COUNT = 256;

char* get_free_blk(data_blks* blks) {
	size_t free_idx = 0;
	for (size_t i = 0; i < blks->n_blks; i++) {
		if (blks->blk_status[i] == false) {
			free_idx = i;
			blks->blk_status[i] = true;
			break;
		}
	}

	return blks->data + (free_idx * blks->blk_sz);
}

void init_default(super_blk* fs) {
	inode* root = &fs->inodes[0];
	inode* hello = &fs->inodes[1];
	root->data = NULL;
	memcpy(root->path, "/", 1);
	root->mode = 0040755;
	root->used = true;

	hello->data = get_free_blk(&fs->data);
	memcpy(hello->data, "hello", 5);
	memcpy(hello->path, "/hello.txt", 10);
	hello->mode = 0040444;
	hello->used = true;
}

super_blk* init_fs(const char* path) {
	// TODO: May be useful to not hardcode sizes of arrays in super_blk
	int fd = open(path, O_CREAT | O_RDWR, 0644);
	assert(fd != -1);

	int sz = NUFS_SIZE + sizeof(super_blk);
	assert(ftruncate(fd, sz) == 0);
	super_blk* fs = NULL;
	assert((fs = mmap(0, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) != MAP_FAILED);

	fs->data.blk_sz = NUFS_SIZE / PAGE_COUNT;
	fs->data.n_blks = PAGE_COUNT;
	fs->data.data = (char*)(fs + 1);

	init_default(fs);
	
	return fs;
}

const inode* get_inode(const super_blk* fs, const char* path) {
	size_t path_len = strlen(path);
	for (size_t i = 0; i < sizeof(fs->inodes) / sizeof(inode); i++) {
		const inode* node = &fs->inodes[i];
		if (!node->used) {
			continue;
		}
		
		size_t n_path_len = strlen(node->path);
		if (n_path_len == path_len && strncmp(node->path, path, path_len) == 0) {
			return node;
		}
	}

	return NULL;
}

int fs_access(const super_blk* fs, const char* path, int mask) {
	return 0;
}

int fs_getattr(const super_blk* fs, const char* path, struct stat *st) {
	const inode* n = get_inode(fs, path);
	if (n == NULL) {
		printf("enoent\n");
		return -ENOENT;
	}

	memset(st, 0, sizeof(struct stat));
	st->st_uid = getuid();
	st->st_gid = getgid();
	st->st_mode = n->mode;
	if (n->data) {
		st->st_size = strlen(n->data);
	} else {
		st->st_size = 0;
	}
	
	return 0;
}

int fs_readdir(const super_blk* fs, const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi) {
	(void) offset;
	(void) fi;
	if (strncmp(path, "/", 1) != 0) {
		return -ENOENT;
	}

	struct stat st;
	if (fs_getattr(fs, path, &st) != 0) {
		return -ENOENT;
	}
	
	filler(buf, ".", &st, 0);
	filler(buf, "..", &st, 0);
	
	size_t path_len = strlen(path);
	for (size_t i = 0; i < sizeof(fs->inodes) / sizeof(inode); i++) {
		inode n = fs->inodes[i];
		if (!n.used) {
			continue;
		}
		
		if (strncmp(n.path, path, path_len) == 0 && strlen(n.path) != path_len) {
			if(fs_getattr(fs, n.path, &st) != 0) {
				return -ENOENT;
			}
			filler(buf, n.path + path_len, NULL, 0);
		}
	}
	
	return 0;
}

int fs_rename(const super_blk* fs, const char* from, const char* to) {
        // Get respective inode
        inode* node = get_inode(fs, from);

        if (node == 0 || node == NULL) {
                return -1;
        }
        memset(node->path, '\0', strlen(node->path));
        memcpy(node->path, to, strlen(to));

        
        return 0;
}

int fs_read(const super_blk* fs, const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
        const inode* node = get_inode(fs, path);

        if (node == 0 || node == NULL) {
                return -1;
        }
        
        char* data = node->data;

        int len = strlen(data) + 1;

        if (size < len) {
                len = size;
        }

        char* src = data + offset;

        //TODO: Error handle for size_t != len
        memcpy(buf, src, len);

        return 0;
}

int fs_write(const super_blk* fs, const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
        const inode* node = get_inode(fs, path);

        if (node == NULL) {
                return -1;
        }

        char* data = node->data;

        int len = strlen(buf) + 1;

        if (size < len) {
                len = size;
        }

        char* dest = data + offset;

        //TODO: Error handle for destination being too small for data
        memcpy((void*)dest, buf, len);

        return 0;
}
