#ifndef __FILESYSTEM_H
#define __FILESYSTEM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "lfs.h"

typedef struct {
    uint32_t total_size;
    uint32_t used_size;
} FSInfo;
typedef lfs_file_t File;
typedef lfs_dir_t Directory;
typedef struct lfs_info FileInfo;

// Open flags
#define FS_O_RDONLY LFS_O_RDONLY
#define FS_O_WRONLY LFS_O_WRONLY
#define FS_O_RDWR   LFS_O_RDWR
#define FS_O_CREAT  LFS_O_CREAT
#define FS_O_EXCL   LFS_O_EXCL
#define FS_O_TRUNC  LFS_O_TRUNC
#define FS_O_APPEND LFS_O_APPEND

// Seek flags
#define FS_SEEK_SET LFS_SEEK_SET
#define FS_SEEK_CUR LFS_SEEK_CUR
#define FS_SEEK_END LFS_SEEK_END

void FS_Init(bool auto_format);
void FS_Deinit(void);
void FS_Format(void);
void FS_Stat(FSInfo* info);
int FS_Mkdir(const char* path);
int FS_Remove(const char* path);
int FS_Rename(const char* old_path, const char* new_path);

int FS_Open(File* file, const char* path, int flags);
int FS_Close(File* file);
int FS_Read(File* file, void* buffer, uint32_t len);
int FS_Write(File* file, const void* buffer, uint32_t len);
int FS_Seek(File* file, int offset, int whence);
int FS_Tell(File* file);

int FS_OpenDir(Directory* dir, const char* path);
int FS_CloseDir(Directory* dir);
int FS_ReadDir(Directory* dir, FileInfo* info);

#endif
