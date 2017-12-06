#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>

#define NUFS_SIZE (1024 * 1024)
#define PAGE_COUNT (256)
#define PAGE_SIZE (NUFS_SIZE / PAGE_COUNT)

typedef struct data_blk_info {
	size_t blk_status_idx;
	size_t offset;
} data_blk_info;

typedef struct inode {
	char path[256];
	int mode;
        int references;
        bool is_hlink;
        int link_idx;
	data_blk_info db_info;
	time_t accessed_at;
	time_t modified_at;
	time_t changed_at;
	int data_size;
} inode;

typedef struct data_blks {
	size_t blk_sz;
	size_t n_blks;
	bool blk_status[255]; // false = open, true = used
	size_t data_offset;
} data_blks;

typedef struct super_blk {
	inode inodes[255];
	data_blks data;
} super_blk;

super_blk* init_fs(const char* path);
void close_fs(super_blk* fs);

int fs_access(const super_blk* fs, const char* path, int mask);
int fs_getattr(const super_blk* fs, const char* path, struct stat *st);
int fs_readdir(const super_blk* fs, const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi);
int fs_rename(const super_blk* fs, const char* from, const char* to);
int fs_read(const super_blk* fs, const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int fs_write(const super_blk* fs, const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int fs_mknod(super_blk* fs, const char* path, mode_t mode, dev_t dev);
int fs_utimens(super_blk* fs, const char* path, const struct timespec ts[2]);
int fs_chmod(const super_blk* fs, const char* path, mode_t mode);
int fs_unlink(super_blk* fs, const char* path);
int fs_truncate(super_blk* fs, const char* path, off_t size);
int fs_link(super_blk* fs, const char* src, const char* dst);
