#include "readonly_blob_core.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool valid_fd(const readonly_blob_core_t *core, int fd)
{
    return core != NULL && fd >= 0 && fd < READONLY_BLOB_MAX_OPEN_FILES
        && core->open[fd];
}

static bool path_matches(const readonly_blob_core_t *core, const char *path)
{
    if (core == NULL || path == NULL) {
        return false;
    }
    if (path[0] == '/') {
        ++path;
    }
    return strcmp(path, core->file_name) == 0;
}

static int fill_stat(const readonly_blob_core_t *core, struct stat *metadata)
{
    if (core == NULL || metadata == NULL) {
        errno = EINVAL;
        return -1;
    }
    const off_t file_size = (off_t)core->size_bytes;
    if (file_size < 0 || (uintmax_t)file_size != (uintmax_t)core->size_bytes) {
        errno = EOVERFLOW;
        return -1;
    }
    memset(metadata, 0, sizeof(*metadata));
    metadata->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;
    metadata->st_nlink = 1;
    metadata->st_size = file_size;
    return 0;
}

bool readonly_blob_core_init(readonly_blob_core_t *core,
                             const uint8_t *data,
                             size_t size_bytes,
                             const char *file_name)
{
    if (core == NULL || data == NULL || size_bytes == 0U || file_name == NULL) {
        return false;
    }
    const off_t file_size = (off_t)size_bytes;
    const ssize_t maximum_read = (ssize_t)size_bytes;
    if (file_size < 0 || maximum_read < 0
        || (uintmax_t)file_size != (uintmax_t)size_bytes
        || (uintmax_t)maximum_read != (uintmax_t)size_bytes) {
        return false;
    }
    const size_t name_length = strlen(file_name);
    if (name_length == 0U || name_length >= READONLY_BLOB_FILE_NAME_CAPACITY
        || strchr(file_name, '/') != NULL) {
        return false;
    }
    memset(core, 0, sizeof(*core));
    core->data = data;
    core->size_bytes = size_bytes;
    memcpy(core->file_name, file_name, name_length + 1U);
    return true;
}

int readonly_blob_core_open(readonly_blob_core_t *core,
                            const char *path,
                            int flags)
{
    if (core == NULL || !path_matches(core, path)) {
        errno = ENOENT;
        return -1;
    }
    if ((flags & O_ACCMODE) != O_RDONLY
        || (flags & (O_CREAT | O_TRUNC | O_APPEND)) != 0) {
        errno = EROFS;
        return -1;
    }
    for (int fd = 0; fd < READONLY_BLOB_MAX_OPEN_FILES; ++fd) {
        if (!core->open[fd]) {
            core->open[fd] = true;
            core->position[fd] = 0U;
            return fd;
        }
    }
    errno = EMFILE;
    return -1;
}

int readonly_blob_core_close(readonly_blob_core_t *core, int fd)
{
    if (!valid_fd(core, fd)) {
        errno = EBADF;
        return -1;
    }
    core->open[fd] = false;
    core->position[fd] = 0U;
    return 0;
}

ssize_t readonly_blob_core_read(readonly_blob_core_t *core,
                                int fd,
                                void *destination,
                                size_t size_bytes)
{
    if (!valid_fd(core, fd)) {
        errno = EBADF;
        return -1;
    }
    if (destination == NULL && size_bytes != 0U) {
        errno = EINVAL;
        return -1;
    }
    const size_t available = core->size_bytes - core->position[fd];
    const size_t count = size_bytes < available ? size_bytes : available;
    if (count != 0U) {
        memcpy(destination, core->data + core->position[fd], count);
        core->position[fd] += count;
    }
    return (ssize_t)count;
}

ssize_t readonly_blob_core_pread(readonly_blob_core_t *core,
                                 int fd,
                                 void *destination,
                                 size_t size_bytes,
                                 off_t offset)
{
    if (!valid_fd(core, fd)) {
        errno = EBADF;
        return -1;
    }
    if ((destination == NULL && size_bytes != 0U) || offset < 0) {
        errno = EINVAL;
        return -1;
    }
    const uint64_t position = (uint64_t)offset;
    if (position > (uint64_t)core->size_bytes) {
        return 0;
    }
    const size_t available = core->size_bytes - (size_t)position;
    const size_t count = size_bytes < available ? size_bytes : available;
    if (count != 0U) {
        memcpy(destination, core->data + (size_t)position, count);
    }
    return (ssize_t)count;
}

off_t readonly_blob_core_lseek(readonly_blob_core_t *core,
                               int fd,
                               off_t offset,
                               int whence)
{
    if (!valid_fd(core, fd)) {
        errno = EBADF;
        return (off_t)-1;
    }
    uintmax_t base;
    switch (whence) {
    case SEEK_SET:
        base = 0;
        break;
    case SEEK_CUR:
        base = (uintmax_t)core->position[fd];
        break;
    case SEEK_END:
        base = (uintmax_t)core->size_bytes;
        break;
    default:
        errno = EINVAL;
        return (off_t)-1;
    }
    uintmax_t requested;
    if (offset >= 0) {
        const uintmax_t increment = (uintmax_t)offset;
        if (increment > (uintmax_t)core->size_bytes - base) {
            errno = EINVAL;
            return (off_t)-1;
        }
        requested = base + increment;
    } else {
        /* -(OFF_MIN) is not representable, so form its magnitude safely. */
        const uintmax_t decrement = (uintmax_t)(-(offset + 1)) + UINTMAX_C(1);
        if (decrement > base) {
            errno = EINVAL;
            return (off_t)-1;
        }
        requested = base - decrement;
    }
    if (requested > (uintmax_t)core->size_bytes) {
        errno = EINVAL;
        return (off_t)-1;
    }
    core->position[fd] = (size_t)requested;
    return (off_t)requested;
}

int readonly_blob_core_fstat(const readonly_blob_core_t *core,
                             int fd,
                             struct stat *metadata)
{
    if (!valid_fd(core, fd)) {
        errno = EBADF;
        return -1;
    }
    return fill_stat(core, metadata);
}

int readonly_blob_core_stat(const readonly_blob_core_t *core,
                            const char *path,
                            struct stat *metadata)
{
    if (!path_matches(core, path)) {
        errno = ENOENT;
        return -1;
    }
    return fill_stat(core, metadata);
}

bool readonly_blob_core_has_open_files(const readonly_blob_core_t *core)
{
    if (core == NULL) {
        return false;
    }
    for (int fd = 0; fd < READONLY_BLOB_MAX_OPEN_FILES; ++fd) {
        if (core->open[fd]) {
            return true;
        }
    }
    return false;
}
