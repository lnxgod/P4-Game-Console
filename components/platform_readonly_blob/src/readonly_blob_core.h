#ifndef PLATFORM_READONLY_BLOB_CORE_H
#define PLATFORM_READONLY_BLOB_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

enum {
    READONLY_BLOB_MAX_OPEN_FILES = 8,
    READONLY_BLOB_FILE_NAME_CAPACITY = 32,
};

typedef struct {
    const uint8_t *data;
    size_t size_bytes;
    char file_name[READONLY_BLOB_FILE_NAME_CAPACITY];
    bool open[READONLY_BLOB_MAX_OPEN_FILES];
    size_t position[READONLY_BLOB_MAX_OPEN_FILES];
} readonly_blob_core_t;

bool readonly_blob_core_init(readonly_blob_core_t *core,
                             const uint8_t *data,
                             size_t size_bytes,
                             const char *file_name);
int readonly_blob_core_open(readonly_blob_core_t *core,
                            const char *path,
                            int flags);
int readonly_blob_core_close(readonly_blob_core_t *core, int fd);
ssize_t readonly_blob_core_read(readonly_blob_core_t *core,
                                int fd,
                                void *destination,
                                size_t size_bytes);
ssize_t readonly_blob_core_pread(readonly_blob_core_t *core,
                                 int fd,
                                 void *destination,
                                 size_t size_bytes,
                                 off_t offset);
off_t readonly_blob_core_lseek(readonly_blob_core_t *core,
                               int fd,
                               off_t offset,
                               int whence);
int readonly_blob_core_fstat(const readonly_blob_core_t *core,
                             int fd,
                             struct stat *metadata);
int readonly_blob_core_stat(const readonly_blob_core_t *core,
                            const char *path,
                            struct stat *metadata);
bool readonly_blob_core_has_open_files(const readonly_blob_core_t *core);

#endif
