// SPDX-License-Identifier: MIT

/*
 * Console OS compatibility adapter for the proven Doom composite.  The
 * standalone app still uses the immutable linker blob implementation; only
 * Console OS resolves this component name to the validated game-data volume.
 */

#include "platform/readonly_blob.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "esp_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "platform/game_storage.h"

typedef struct {
    FILE *files[PLATFORM_READONLY_BLOB_MAX_OPEN_FILES];
    StaticSemaphore_t lock_storage;
    SemaphoreHandle_t lock;
    bool registered;
} game_storage_blob_context_t;

static game_storage_blob_context_t s_context;

_Static_assert(sizeof(off_t) <= sizeof(long),
               "Doom storage adapter requires off_t to fit in long");

static bool take_lock(game_storage_blob_context_t *context)
{
    if (context == NULL || context->lock == NULL ||
        xSemaphoreTake(context->lock, portMAX_DELAY) != pdTRUE) {
        errno = EBUSY;
        return false;
    }
    return true;
}

static void give_lock(game_storage_blob_context_t *context)
{
    (void)xSemaphoreGive(context->lock);
}

static bool path_matches(const char *path)
{
    if (path == NULL) {
        return false;
    }
    if (path[0] == '/') {
        ++path;
    }
    return strcmp(path, "doom1.wad") == 0;
}

static bool valid_fd(const game_storage_blob_context_t *context, int fd)
{
    return context != NULL && fd >= 0 &&
        fd < PLATFORM_READONLY_BLOB_MAX_OPEN_FILES &&
        context->files[fd] != NULL;
}

static int fill_stat(struct stat *metadata)
{
    if (metadata == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(metadata, 0, sizeof(*metadata));
    metadata->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;
    metadata->st_nlink = 1;
    metadata->st_size = (off_t)PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES;
    return 0;
}

static int storage_open(void *opaque, const char *path, int flags, int mode)
{
    (void)mode;
    game_storage_blob_context_t *const context = opaque;
    if (!path_matches(path)) {
        errno = ENOENT;
        return -1;
    }
    if ((flags & O_ACCMODE) != O_RDONLY ||
        (flags & (O_CREAT | O_TRUNC | O_APPEND)) != 0) {
        errno = EROFS;
        return -1;
    }
    if (!platform_game_storage_game_locked()) {
        errno = EBUSY;
        return -1;
    }
    if (!take_lock(context)) {
        return -1;
    }
    int fd = -1;
    for (int candidate = 0;
         candidate < PLATFORM_READONLY_BLOB_MAX_OPEN_FILES; ++candidate) {
        if (context->files[candidate] == NULL) {
            fd = candidate;
            break;
        }
    }
    if (fd < 0) {
        errno = EMFILE;
        give_lock(context);
        return -1;
    }
    FILE *const file = fopen(PLATFORM_GAME_STORAGE_DOOM_WAD_PATH, "rb");
    if (file == NULL) {
        give_lock(context);
        return -1;
    }
    context->files[fd] = file;
    give_lock(context);
    return fd;
}

static int storage_close(void *opaque, int fd)
{
    game_storage_blob_context_t *const context = opaque;
    if (!take_lock(context)) {
        return -1;
    }
    if (!valid_fd(context, fd)) {
        errno = EBADF;
        give_lock(context);
        return -1;
    }
    FILE *const file = context->files[fd];
    context->files[fd] = NULL;
    const int result = fclose(file);
    give_lock(context);
    return result;
}

static ssize_t storage_read(void *opaque, int fd,
                            void *destination, size_t size_bytes)
{
    game_storage_blob_context_t *const context = opaque;
    if (!take_lock(context)) {
        return -1;
    }
    if (!valid_fd(context, fd)) {
        errno = EBADF;
        give_lock(context);
        return -1;
    }
    if (destination == NULL && size_bytes != 0U) {
        errno = EINVAL;
        give_lock(context);
        return -1;
    }
    if (size_bytes > (size_t)INT_MAX) {
        size_bytes = (size_t)INT_MAX;
    }
    const size_t count = fread(destination, 1U, size_bytes,
                               context->files[fd]);
    const ssize_t result = count == 0U && ferror(context->files[fd]) != 0
        ? -1 : (ssize_t)count;
    give_lock(context);
    return result;
}

static ssize_t storage_pread(void *opaque, int fd, void *destination,
                             size_t size_bytes, off_t offset)
{
    game_storage_blob_context_t *const context = opaque;
    if (!take_lock(context)) {
        return -1;
    }
    if (!valid_fd(context, fd)) {
        errno = EBADF;
        give_lock(context);
        return -1;
    }
    if ((destination == NULL && size_bytes != 0U) || offset < 0) {
        errno = EINVAL;
        give_lock(context);
        return -1;
    }
    if ((uint64_t)offset > PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES) {
        give_lock(context);
        return 0;
    }
    if (size_bytes > (size_t)INT_MAX) {
        size_bytes = (size_t)INT_MAX;
    }
    FILE *const file = context->files[fd];
    const long original = ftell(file);
    if (original < 0L || fseek(file, (long)offset, SEEK_SET) != 0) {
        give_lock(context);
        return -1;
    }
    clearerr(file);
    const size_t count = fread(destination, 1U, size_bytes, file);
    int saved_errno = errno;
    ssize_t result = count == 0U && ferror(file) != 0
        ? -1 : (ssize_t)count;
    if (fseek(file, original, SEEK_SET) != 0) {
        result = -1;
        saved_errno = errno;
    }
    errno = saved_errno;
    give_lock(context);
    return result;
}

static off_t storage_lseek(void *opaque, int fd, off_t offset, int whence)
{
    game_storage_blob_context_t *const context = opaque;
    if (!take_lock(context)) {
        return (off_t)-1;
    }
    if (!valid_fd(context, fd)) {
        errno = EBADF;
        give_lock(context);
        return (off_t)-1;
    }
    FILE *const file = context->files[fd];
    if (fseek(file, (long)offset, whence) != 0) {
        give_lock(context);
        return (off_t)-1;
    }
    const long position = ftell(file);
    give_lock(context);
    return position < 0L ? (off_t)-1 : (off_t)position;
}

static int storage_fstat(void *opaque, int fd, struct stat *metadata)
{
    game_storage_blob_context_t *const context = opaque;
    if (!take_lock(context)) {
        return -1;
    }
    const int result = valid_fd(context, fd) ? fill_stat(metadata) : -1;
    if (!valid_fd(context, fd)) {
        errno = EBADF;
    }
    give_lock(context);
    return result;
}

#ifdef CONFIG_VFS_SUPPORT_DIR
static int storage_stat(void *opaque, const char *path, struct stat *metadata)
{
    game_storage_blob_context_t *const context = opaque;
    if (!path_matches(path)) {
        errno = ENOENT;
        return -1;
    }
    if (!take_lock(context)) {
        return -1;
    }
    const int result = fill_stat(metadata);
    give_lock(context);
    return result;
}

static const esp_vfs_dir_ops_t s_directory_operations = {
    .stat_p = storage_stat,
};
#endif

static const esp_vfs_fs_ops_t s_operations = {
    .lseek_p = storage_lseek,
    .read_p = storage_read,
    .pread_p = storage_pread,
    .open_p = storage_open,
    .close_p = storage_close,
    .fstat_p = storage_fstat,
#ifdef CONFIG_VFS_SUPPORT_DIR
    .dir = &s_directory_operations,
#endif
};

esp_err_t platform_readonly_blob_register(
    const platform_readonly_blob_config_t *config)
{
    if (config == NULL || config->base_path == NULL ||
        config->file_name == NULL || config->data == NULL ||
        strcmp(config->base_path, "/doom") != 0 ||
        strcmp(config->file_name, "doom1.wad") != 0 ||
        config->size_bytes != (size_t)PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!platform_game_storage_game_locked() || s_context.registered) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_context.lock == NULL) {
        s_context.lock = xSemaphoreCreateMutexStatic(&s_context.lock_storage);
        if (s_context.lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    const int flags = ESP_VFS_FLAG_CONTEXT_PTR | ESP_VFS_FLAG_READONLY_FS |
        ESP_VFS_FLAG_STATIC;
    const esp_err_t result = esp_vfs_register_fs(
        "/doom", &s_operations, flags, &s_context);
    if (result == ESP_OK) {
        s_context.registered = true;
    }
    return result;
}

esp_err_t platform_readonly_blob_unregister(void)
{
    if (!s_context.registered || !take_lock(&s_context)) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t index = 0U;
         index < PLATFORM_READONLY_BLOB_MAX_OPEN_FILES; ++index) {
        if (s_context.files[index] != NULL) {
            give_lock(&s_context);
            return ESP_ERR_INVALID_STATE;
        }
    }
    const esp_err_t result = esp_vfs_unregister_fs("/doom");
    if (result == ESP_OK) {
        s_context.registered = false;
    }
    give_lock(&s_context);
    return result;
}
