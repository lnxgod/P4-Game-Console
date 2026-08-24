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
    uint64_t file_sizes[PLATFORM_READONLY_BLOB_MAX_OPEN_FILES];
    StaticSemaphore_t lock_storage;
    SemaphoreHandle_t lock;
    platform_game_storage_doom_title_t title;
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

static bool resolve_path(
    const game_storage_blob_context_t *context,
    const char *path,
    const char **physical_path,
    uint64_t *size_bytes)
{
    if (context == NULL || path == NULL || physical_path == NULL ||
        size_bytes == NULL) {
        return false;
    }
    if (path[0] == '/') {
        ++path;
    }
    if (context->title == PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM &&
        strcmp(path, "doom1.wad") == 0) {
        *physical_path = PLATFORM_GAME_STORAGE_DOOM_WAD_PATH;
        *size_bytes = PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES;
        return true;
    }
    if (context->title == PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST) {
        if (strcmp(path, "chex.wad") == 0) {
            *physical_path = PLATFORM_GAME_STORAGE_CHEX_WAD_PATH;
            *size_bytes = PLATFORM_GAME_STORAGE_CHEX_WAD_BYTES;
            return true;
        }
        if (strcmp(path, "chex.deh") == 0) {
            *physical_path = PLATFORM_GAME_STORAGE_CHEX_DEH_PATH;
            *size_bytes = PLATFORM_GAME_STORAGE_CHEX_DEH_BYTES;
            return true;
        }
    }
    return false;
}

static bool valid_fd(const game_storage_blob_context_t *context, int fd)
{
    return context != NULL && fd >= 0 &&
        fd < PLATFORM_READONLY_BLOB_MAX_OPEN_FILES &&
        context->files[fd] != NULL;
}

static int fill_stat(struct stat *metadata, uint64_t size_bytes)
{
    if (metadata == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(metadata, 0, sizeof(*metadata));
    metadata->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;
    metadata->st_nlink = 1;
    metadata->st_size = (off_t)size_bytes;
    return 0;
}

static int storage_open(void *opaque, const char *path, int flags, int mode)
{
    (void)mode;
    game_storage_blob_context_t *const context = opaque;
    const char *physical_path = NULL;
    uint64_t size_bytes = 0U;
    if (!resolve_path(
            context, path, &physical_path, &size_bytes)) {
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
    FILE *const file = fopen(physical_path, "rb");
    if (file == NULL) {
        give_lock(context);
        return -1;
    }
    context->files[fd] = file;
    context->file_sizes[fd] = size_bytes;
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
    context->file_sizes[fd] = 0U;
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
    if ((uint64_t)offset > context->file_sizes[fd]) {
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
    const bool valid = valid_fd(context, fd);
    const int result = valid
        ? fill_stat(metadata, context->file_sizes[fd]) : -1;
    if (!valid) {
        errno = EBADF;
    }
    give_lock(context);
    return result;
}

#ifdef CONFIG_VFS_SUPPORT_DIR
static int storage_stat(void *opaque, const char *path, struct stat *metadata)
{
    game_storage_blob_context_t *const context = opaque;
    const char *physical_path = NULL;
    uint64_t size_bytes = 0U;
    if (!resolve_path(
            context, path, &physical_path, &size_bytes)) {
        errno = ENOENT;
        return -1;
    }
    if (!take_lock(context)) {
        return -1;
    }
    const int result = fill_stat(metadata, size_bytes);
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
    const bool doom_config = config != NULL &&
        config->file_name != NULL &&
        strcmp(config->file_name, "doom1.wad") == 0 &&
        config->size_bytes ==
            (size_t)PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES;
    const bool chex_config = config != NULL &&
        config->file_name != NULL &&
        strcmp(config->file_name, "chex.wad") == 0 &&
        config->size_bytes ==
            (size_t)PLATFORM_GAME_STORAGE_CHEX_WAD_BYTES;
    if (config == NULL || config->base_path == NULL ||
        config->file_name == NULL || config->data == NULL ||
        strcmp(config->base_path, "/doom") != 0 ||
        (!doom_config && !chex_config)) {
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
    s_context.title = chex_config
        ? PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST
        : PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM;
    const esp_err_t result = esp_vfs_register_fs(
        "/doom", &s_operations, flags, &s_context);
    if (result == ESP_OK) {
        s_context.registered = true;
    } else {
        s_context.title = PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM;
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
        s_context.title = PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM;
    }
    give_lock(&s_context);
    return result;
}
