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

data_blk_info get_free_blk(data_blks* blks) {
	size_t free_idx = 0;
	for (size_t i = 0; i < blks->n_blks; i++) {
		if (blks->blk_status[i] == false) {
			free_idx = i;
			blks->blk_status[i] = true;
			break;
		}
	}

	data_blk_info r;
	r.blk_status_idx = free_idx;
	r.offset = blks->data_offset + (free_idx * blks->blk_sz);

	return r;
}

char* fs_dataptr(const super_blk* fs,  inode* n) {
        return ((char*)fs) + n->db_info.offset;
}

void init_default(super_blk* fs) {
	struct stat st;
	if (fs_getattr(fs, "/", &st) != 0) {
		fs_mknod(fs, "/", 0040755, 0);		
	}

	if (fs_getattr(fs, "/hello.txt", &st) != 0) {
		fs_mknod(fs, "/hello.txt", 0100644, 0);
		fs_write(fs, "/hello.txt", "hello\n", 6, 0, NULL);
	}
}

super_blk* init_fs(const char* path) {
	int fd = open(path, O_CREAT | O_RDWR, 0644);
	assert(fd != -1);

	int sz = NUFS_SIZE + sizeof(super_blk);
	assert(ftruncate(fd, sz) == 0);
	super_blk* fs = NULL;
	assert((fs = mmap(0, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) != MAP_FAILED);

	fs->data.blk_sz = NUFS_SIZE / PAGE_COUNT;
	fs->data.n_blks = PAGE_COUNT;
	fs->data.data_offset = sizeof(super_blk);

	init_default(fs);
	
	return fs;
}

int find_inode_idx(const super_blk* fs, const char* path) {
	size_t path_len = strlen(path);
	for (size_t i = 0; i < sizeof(fs->inodes) / sizeof(inode); i++) {
		const inode* node = &fs->inodes[i];
		if (node->references < 1) {
			continue;
		}

		if (node->path[0] == '\0') {
			continue;
		}

		size_t node_len = strlen(node->path);
		if (node_len == path_len && strncmp(path, node->path, node_len) == 0) {
			return i;
		}
	}
	return -1;
}

const inode* get_inode(const super_blk* fs, const char* path) {
	int index = find_inode_idx(fs, path);

        if (index == -1) {
                return NULL;
        }

	return &fs->inodes[index];
}

const inode* get_hlink_root(const super_blk* fs, const char* path) {
        const inode* node = get_inode(fs, path);
                
        if (!node) {
                return NULL;
        }

        if (node->is_hlink) {
                inode next = fs->inodes[node->link_idx];
                return get_hlink_root(fs, next.path);
        } else {
                return node;
        }
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
	st->st_atim.tv_sec = n->accessed_at;
	st->st_mtim.tv_sec = n->modified_at;
	st->st_ctim.tv_sec = n->changed_at;
	st->st_nlink = n->references;
	if (n->is_hlink) {
		const inode* r = get_hlink_root(fs, path);
		if (r == NULL) {
			return -ENOENT;
		}
		st->st_size = r->data_size;
	} else if (n->db_info.offset != 0) {
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
		if (n.mode == 0) {
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
        inode* root = (inode*)get_hlink_root(fs, path);

        if (node == NULL || root == NULL) {
                return -ENOENT;
        }
        
        if (!check_mode(node, 4)) {
                return -EACCES;
        }

        char* data = fs_dataptr(fs, root);

        // Can't read past data inside file
        if (offset > root->data_size) {
                return -ENOMEM;
        }

        int read_size = root->data_size;
        char* read_ptr = data + offset;

        // min(size to read, size of file from read_start to EOF)
        read_size = size < read_size ? size : read_size;

        int to_end = root->data_size - offset;
        read_size = to_end < read_size ? to_end : read_size;

        memcpy(buf, read_ptr, read_size);

        time_t t = time(NULL);
        node->accessed_at = t;

        // Number of bytes read
        return read_size;
}

// Write data to file
int fs_write(const super_blk* fs, const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
        inode* node = (inode*)get_hlink_root(fs, path);

        if (node == NULL) {
                return -ENOENT;
        }

        if (!check_mode(node, 2)) {
                return -EACCES;
        }
        
        char* data = fs_dataptr(fs, node);
        
        char* write_ptr = data + offset;

        // If end of write puts you past allocated memory
        if (write_ptr + size > data + fs->data.blk_sz
            || offset > fs->data.blk_sz) { // Or would start you OOB
                return -ENOMEM;
        }

        memcpy(write_ptr, buf, size);

        time_t t = time(NULL);
        node->modified_at = t;
        node->accessed_at = t;
        node->changed_at = t;

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
	
	data_blk_info data_blk = get_free_blk(&fs->data);
	if (data_blk.offset == 0) {
		return -ENOMEM;
	}
	
	n->mode = mode;
	memcpy(n->path, path, strlen(path));
	n->db_info = data_blk;
        n->references = 1;

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

int fs_unlink(super_blk* fs, const char* path) {
	inode* n = (inode*)get_inode(fs, path);
	if (n == NULL) {
		return -ENOENT;
	}

	if (n->is_hlink) {
		inode* root = (inode*)get_hlink_root(fs, path);
		root->references -= 1;
	} else {
		if (n->references == 0) {
			fs->data.blk_status[n->db_info.blk_status_idx] = false;
			n->db_info.blk_status_idx = -1;
			n->db_info.offset = 0;
			n->data_size = 0;
		}
	}

	n->mode = 0;
	n->references -= 1;
	n->accessed_at = 0;
	n->modified_at = 0;
	n->changed_at = 0;

	return 0;
}

int fs_truncate(super_blk* fs, const char* path, off_t size) {
	inode* n = (inode*)get_hlink_root(fs, path);
	if (n == NULL) {
		return -ENOENT;
	}

	if (n->db_info.offset == 0) {
		return -1;
	}

	if (size > PAGE_SIZE) {
		return -ENOMEM;
	} 

	n->data_size = size;
	return 0;
}



int fs_link(super_blk* fs, const char* src, const char* dst) {
        const inode* source = get_inode(fs, src);

        int idx = find_inode_idx(fs, src);

        if (idx == -1) {
                return -ENOENT;
        }

        inode* original = &fs->inodes[idx];

        if (source->references == 0) {
                return -ENOENT;
        }

        original->references += 1;

        inode* node = fs_get_free_inode(fs);
        memcpy(node->path, dst, strlen(dst));
        node->mode = original->mode;
        node->references = 1;
        node->is_hlink = true;
        node->link_idx = idx;

        node->db_info.blk_status_idx = -1;
        node->db_info.offset = 0;
        
        time_t t = time(NULL);
        node->modified_at = t;
        node->accessed_at = t;
        node->changed_at = t;

        node->data_size = -1;
        
	return 0;
}
