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
	root->accessed_at = time(NULL);
	root->changed_at = root->accessed_at;
	root->modified_at = root->accessed_at;
  root->data_size = 0;

	hello->data = get_free_blk(&fs->data);
	memcpy(hello->data, "hello", 5);
	memcpy(hello->path, "/hello.txt", 10);
	hello->mode = 0100444;
	hello->accessed_at = time(NULL);
	hello->changed_at = hello->accessed_at;
	hello->modified_at = hello->accessed_at;
	hello->mode = 0100777;
	hello->data_size = 5;
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
		if (node->path[0] == '\0') {
			continue;
		}
		
		size_t n_path_len = strlen(node->path);
		if (n_path_len == path_len && strncmp(node->path, path, path_len) == 0) {
			return node;
		}
	}

	return NULL;
}

int check_mode(const inode* n, int mode) {
	// NOTE: we only check owner perms
	int flags = (n->mode & 0b111000000) >> 6;
	return (flags & mode) != 0;
}

int fs_access(const super_blk* fs, const char* path, int mask) {
	inode* n = (inode*)get_inode(fs, path);
	if (!n) {
		return -ENOENT;
	}

	if (mask == F_OK) {
		return 0;
	}

	if (!check_mode(n, mask)) {
		return -EACCES;
	}

	return 0;
}

int fs_getattr(const super_blk* fs, const char* path, struct stat *st) {
	const inode* n = get_inode(fs, path);
	if (n == NULL) {
		return -ENOENT;
	}

	memset(st, 0, sizeof(struct stat));
	st->st_uid = getuid();
	st->st_gid = getgid();
	st->st_mode = n->mode;
	if (n->data) {
		st->st_size = n->data_size;
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
		if (n.path[0] == '\0') {
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
        inode* node = (inode*)get_inode(fs, from);

        if (node == NULL) {
                return -ENOENT;
        }

        memset(node->path, '\0', strlen(node->path));
        memcpy(node->path, to, strlen(to));

        time_t t = time(NULL);
        node->changed_at = t;

        return 0;
}

int fs_read(const super_blk* fs, const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
        inode* node = (inode*)get_inode(fs, path);

        if (node == NULL) {
                return -ENOENT;
        }
        
        char* data = node->data;

        // Can't read past data inside file
        if (offset > node->data_size) {
                return -ENOMEM;
        }

        int read_size = node->data_size;
        char* src = data + offset;

        // min(size to read, size of file from read_start to EOF)
        read_size = size < read_size ? size : read_size;

        int to_end = node->data_size - offset;
        read_size = to_end < read_size ? to_end : read_size;

        memcpy(buf, src, read_size);

        time_t t = time(NULL);
        node->accessed_at = t;

        // Number of bytes read
        return read_size;
}

// Write data to file
int fs_write(const super_blk* fs, const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
        inode* node = (inode*)get_inode(fs, path);

        if (node == NULL) {
                return -ENOENT;
        }
        
        char* data = node->data;
        
        char* write_point = data + offset;

        // If end of write puts you past allocated memory
        if (write_point + size > data + fs->data.blk_sz
            || offset > fs->data.blk_sz) { // Or would start you OOB
                return -ENOMEM;
        }

        memcpy(write_point, buf, size);

        time_t t = time(NULL);
        node->modified_at = t;

        node->data_size = node->data_size + size;
        
        // Number of bytes written
        return size;
}


inode* fs_get_free_inode(super_blk* fs) {
	for (size_t i = 0; i < sizeof(fs->inodes) / sizeof(inode); i++) {
		inode* n = &fs->inodes[i];
		if (n->path[0] == 0) {
			return n;
		}
	}

	return NULL;
}


int fs_mknod(super_blk* fs, const char* path, mode_t mode, dev_t dev) {
	(void) dev;
	
	inode* n = fs_get_free_inode(fs);
	if (n == NULL) {
		return -ENOMEM;
	}
	
	char* data_ptr = get_free_blk(&fs->data);
	if (data_ptr == NULL) {
		return -ENOMEM;
	}
	
	n->mode = mode;
	memcpy(n->path, path, strlen(path));
	n->data = data_ptr;

	return 0;
}

int fs_utimens(super_blk* fs, const char* path, const struct timespec ts[2]) {
	inode* n = (inode*)get_inode(fs, path);
	if (n == NULL) {
		return -ENOENT;
	}
	n->accessed_at = ts[0].tv_sec;
	n->modified_at = ts[1].tv_sec;
	n->changed_at = n->modified_at;

	return 0;
}

int fs_chmod(const super_blk* fs, const char* path, mode_t mode) {
        inode* n = (inode*)get_inode(fs, path);

        n->mode = mode;
        
        time_t t = time(NULL);
        n->changed_at = t;

        return 0;
}
