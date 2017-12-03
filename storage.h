#ifndef NUFS_STORAGE_H
#define NUFS_STORAGE_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#define FUSE_USE_VERSION 26
#include <fuse.h>

void storage_init(const char* path);
int         get_stat(const char* path, struct stat* st);
const char* get_data(const char* path);
int get_access(const char* path, int mask);
int get_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                off_t offset, struct fuse_file_info* fi);

#endif
