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
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "esp_log.h"
#include "esp_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "platform/game_storage.h"

typedef struct {
    const uint8_t *open_data[PLATFORM_READONLY_BLOB_MAX_OPEN_FILES];
    uint64_t file_sizes[PLATFORM_READONLY_BLOB_MAX_OPEN_FILES];
    uint64_t positions[PLATFORM_READONLY_BLOB_MAX_OPEN_FILES];
    const uint8_t *wad_data;
    size_t wad_bytes;
    const uint8_t *deh_data;
    size_t deh_bytes;
    StaticSemaphore_t lock_storage;
    SemaphoreHandle_t lock;
    platform_game_storage_doom_title_t title;
    bool registered;
} game_storage_blob_context_t;

static game_storage_blob_context_t s_context;
static const uint8_t s_arena_marker[3];
static const char *const TAG = "p4_wad_vfs";

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
    uint64_t *size_bytes)
{
    if (context == NULL || path == NULL || size_bytes == NULL) {
        return false;
    }
    if (path[0] == '/') {
        ++path;
    }
    if (context->title == PLATFORM_GAME_STORAGE_DOOM_TITLE_GAME_CHANGERS_AI &&
        strcmp(path,"freedoom2.wad")==0) {
        *size_bytes=PLATFORM_GAME_STORAGE_FREEDOOM2_WAD_BYTES;
        return true;
    }
    if (context->title == PLATFORM_GAME_STORAGE_DOOM_TITLE_GAME_CHANGERS_AI &&
        strcmp(path,"purehades.wad")==0) {
        *size_bytes=PLATFORM_GAME_STORAGE_PUREHADES_WAD_BYTES;
        return true;
    }
    if (context->title == PLATFORM_GAME_STORAGE_DOOM_TITLE_GAME_CHANGERS_AI &&
        strcmp(path,"dwango5.wad")==0) {
        *size_bytes=PLATFORM_GAME_STORAGE_DWANGO5_WAD_BYTES;
        return true;
    }
    if (context->title == PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM &&
        strcmp(path, "doom1.wad") == 0) {
        *size_bytes = PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES;
        return true;
    }
    if (context->title == PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST) {
        if (strcmp(path, "chex.wad") == 0) {
            *size_bytes = PLATFORM_GAME_STORAGE_CHEX_WAD_BYTES;
            return true;
        }
        if (strcmp(path, "chex.deh") == 0) {
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
        context->open_data[fd] != NULL;
}

static const uint8_t *loaded_data_for_path(
    const game_storage_blob_context_t *context,
    const char *path)
{
    if (context == NULL || path == NULL) {
        return NULL;
    }
    if (path[0] == '/') {
        ++path;
    }
    if (context->title == PLATFORM_GAME_STORAGE_DOOM_TITLE_GAME_CHANGERS_AI &&
        strcmp(path,"freedoom2.wad")==0) return &s_arena_marker[P4_GCA_BASE];
    if (context->title == PLATFORM_GAME_STORAGE_DOOM_TITLE_GAME_CHANGERS_AI &&
        strcmp(path,"purehades.wad")==0) return &s_arena_marker[P4_GCA_PWAD];
    if (context->title == PLATFORM_GAME_STORAGE_DOOM_TITLE_GAME_CHANGERS_AI &&
        strcmp(path,"dwango5.wad")==0) return &s_arena_marker[P4_GCA_DWANGO];
    if (context->title == PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM &&
        strcmp(path, "doom1.wad") == 0) {
        return context->wad_data;
    }
    if (context->title == PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST) {
        if (strcmp(path, "chex.wad") == 0) {
            return context->wad_data;
        }
        if (strcmp(path, "chex.deh") == 0) {
            return context->deh_data;
        }
    }
    return NULL;
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
    uint64_t size_bytes = 0U;
    if (!resolve_path(context, path, &size_bytes)) {
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
    const uint8_t *const loaded_data = loaded_data_for_path(context, path);
    if (loaded_data == NULL) {
        errno = EIO;
        give_lock(context);
        return -1;
    }
    int fd = -1;
    for (int candidate = 0;
         candidate < PLATFORM_READONLY_BLOB_MAX_OPEN_FILES; ++candidate) {
        if (context->open_data[candidate] == NULL) {
            fd = candidate;
            break;
        }
    }
    if (fd < 0) {
        errno = EMFILE;
        give_lock(context);
        return -1;
    }
    context->open_data[fd] = loaded_data;
    context->file_sizes[fd] = size_bytes;
    context->positions[fd] = 0U;
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
    context->open_data[fd] = NULL;
    context->file_sizes[fd] = 0U;
    context->positions[fd] = 0U;
    give_lock(context);
    return 0;
}

static ssize_t storage_read_at_locked(
    game_storage_blob_context_t *context,
    int fd,
    void *destination,
    size_t size_bytes,
    uint64_t offset)
{
    if (!valid_fd(context, fd) ||
        (destination == NULL && size_bytes != 0U)) {
        errno = EINVAL;
        return -1;
    }
    if (offset > context->file_sizes[fd]) {
        return 0;
    }
    const uint64_t available = context->file_sizes[fd] - offset;
    if ((uint64_t)size_bytes > available) {
        size_bytes = (size_t)available;
    }
    if (context->title == PLATFORM_GAME_STORAGE_DOOM_TITLE_GAME_CHANGERS_AI) {
        if (platform_game_storage_read_arena_wad(
                (unsigned)(context->open_data[fd]-s_arena_marker),
                (size_t)offset,destination,size_bytes)!=ESP_OK) {
            errno=EIO; return -1;
        }
        return (ssize_t)size_bytes;
    }
    if (size_bytes != 0U) {
        memcpy(destination, context->open_data[fd] + (size_t)offset,
               size_bytes);
    }
    return (ssize_t)size_bytes;
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
    const ssize_t result = storage_read_at_locked(
        context, fd, destination, size_bytes, context->positions[fd]);
    if (result > 0) {
        context->positions[fd] += (uint64_t)result;
    }
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
    const ssize_t result = storage_read_at_locked(
        context, fd, destination, size_bytes, (uint64_t)offset);
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
    uintmax_t base = 0U;
    switch (whence) {
    case SEEK_SET:
        break;
    case SEEK_CUR:
        base = (uintmax_t)context->positions[fd];
        break;
    case SEEK_END:
        base = (uintmax_t)context->file_sizes[fd];
        break;
    default:
        errno = EINVAL;
        give_lock(context);
        return (off_t)-1;
    }
    uintmax_t requested = 0U;
    if (offset >= 0) {
        const uintmax_t increment = (uintmax_t)offset;
        if (base > (uintmax_t)context->file_sizes[fd] ||
            increment > (uintmax_t)context->file_sizes[fd] - base) {
            errno = EINVAL;
            give_lock(context);
            return (off_t)-1;
        }
        requested = base + increment;
    } else {
        const uintmax_t decrement =
            (uintmax_t)(-(offset + 1)) + UINTMAX_C(1);
        if (decrement > base) {
            errno = EINVAL;
            give_lock(context);
            return (off_t)-1;
        }
        requested = base - decrement;
    }
    context->positions[fd] = (uint64_t)requested;
    give_lock(context);
    return (off_t)requested;
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
    uint64_t size_bytes = 0U;
    if (!resolve_path(context, path, &size_bytes)) {
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

static void clear_snapshot(game_storage_blob_context_t *context)
{
    if (context == NULL) {
        return;
    }
    context->deh_data = NULL;
    context->deh_bytes = 0U;
    context->wad_data = NULL;
    context->wad_bytes = 0U;
}

esp_err_t platform_readonly_blob_register(
    const platform_readonly_blob_config_t *config)
{
    const bool arena_config = config && config->file_name &&
        strcmp(config->file_name,"freedoom2.wad")==0 &&
        config->size_bytes==(size_t)PLATFORM_GAME_STORAGE_FREEDOOM2_WAD_BYTES;
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
        (!doom_config && !chex_config && !arena_config)) {
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
    s_context.title = arena_config ? PLATFORM_GAME_STORAGE_DOOM_TITLE_GAME_CHANGERS_AI : chex_config
        ? PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST
        : PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM;
    platform_game_storage_doom_snapshot_t snapshot = {0};
    esp_err_t result;
    if (arena_config) {
        uint8_t probe[12];
        result=platform_game_storage_read_arena_wad(P4_GCA_BASE,0,probe,sizeof(probe));
        if (result==ESP_OK)
            result=platform_game_storage_read_arena_wad(P4_GCA_PWAD,0,probe,sizeof(probe));
        if (result==ESP_OK)
            result=platform_game_storage_read_arena_wad(P4_GCA_DWANGO,0,probe,sizeof(probe));
        snapshot.wad_size_bytes=(size_t)PLATFORM_GAME_STORAGE_FREEDOOM2_WAD_BYTES;
    } else result = platform_game_storage_get_locked_doom_snapshot(s_context.title,&snapshot);
    if (result != ESP_OK) {
        s_context.title = PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM;
        return result;
    }
    s_context.wad_data = snapshot.wad_data;
    s_context.wad_bytes = snapshot.wad_size_bytes;
    s_context.deh_data = snapshot.deh_data;
    s_context.deh_bytes = snapshot.deh_size_bytes;

    const int flags = ESP_VFS_FLAG_CONTEXT_PTR | ESP_VFS_FLAG_READONLY_FS |
        ESP_VFS_FLAG_STATIC;
    result = esp_vfs_register_fs(
        "/doom", &s_operations, flags, &s_context);
    if (result == ESP_OK) {
        s_context.registered = true;
        ESP_LOGI(TAG,
                 "P4_WAD_VFS_IO_READY mode=%s bytes=%u",
                 arena_config ? "verified-sd-blocks" : "verified-psram-snapshot",
                 (unsigned)(s_context.wad_bytes + s_context.deh_bytes));
    } else {
        clear_snapshot(&s_context);
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
        if (s_context.open_data[index] != NULL) {
            give_lock(&s_context);
            return ESP_ERR_INVALID_STATE;
        }
    }
    const esp_err_t result = esp_vfs_unregister_fs("/doom");
    if (result == ESP_OK) {
        clear_snapshot(&s_context);
        s_context.registered = false;
        s_context.title = PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM;
    }
    give_lock(&s_context);
    return result;
}
