#pragma once
#include <stddef.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "esp_err.h"
#define ESP_VFS_FLAG_CONTEXT_PTR 1
#define ESP_VFS_FLAG_READONLY_FS 2
#define ESP_VFS_FLAG_STATIC 4
typedef struct { int (*stat_p)(void *,const char *,struct stat *); } esp_vfs_dir_ops_t;
typedef struct {
    int (*ioctl_p)(void *,int,int,va_list);
    off_t (*lseek_p)(void *,int,off_t,int);
    ssize_t (*read_p)(void *,int,void *,size_t);
    ssize_t (*pread_p)(void *,int,void *,size_t,off_t);
    int (*open_p)(void *,const char *,int,int);
    int (*close_p)(void *,int);
    int (*fstat_p)(void *,int,struct stat *);
    const esp_vfs_dir_ops_t *dir;
} esp_vfs_fs_ops_t;
esp_err_t esp_vfs_register_fs(const char *,const esp_vfs_fs_ops_t *,int,void *);
esp_err_t esp_vfs_unregister_fs(const char *);
