#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/stat.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>

typedef struct inode {
	bool used;
	char path[256];
	int mode;
	char* data;
} inode;

typedef struct data_blks {
	size_t blk_sz;
	size_t n_blks;
	bool blk_status[255]; // false = open, true = used
	char* data;
} data_blks;

typedef struct super_blk {
	inode inodes[255];
	data_blks data;
} super_blk;

super_blk* init_fs(const char* path);
void close_fs(super_blk* fs);

int fs_access(const super_blk* fs, const char* path, int mask);
int fs_getattr(const super_blk* fs, const char* path, struct stat *st);
int fs_readdir(const super_blk* fs, const char* path, void* buf, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info* fi);
