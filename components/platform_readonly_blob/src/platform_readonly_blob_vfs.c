#include "platform/readonly_blob.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "esp_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "readonly_blob_core.h"

enum {
    BASE_PATH_CAPACITY = ESP_VFS_PATH_MAX + 1,
};

typedef struct {
    readonly_blob_core_t core;
    char base_path[BASE_PATH_CAPACITY];
    StaticSemaphore_t lock_storage;
    SemaphoreHandle_t lock;
    bool registered;
} readonly_blob_vfs_context_t;

static readonly_blob_vfs_context_t s_context;

static bool take_lock(readonly_blob_vfs_context_t *context)
{
    if (context == NULL || context->lock == NULL
        || xSemaphoreTake(context->lock, portMAX_DELAY) != pdTRUE) {
        errno = EBUSY;
        return false;
    }
    return true;
}

static void give_lock(readonly_blob_vfs_context_t *context)
{
    (void)xSemaphoreGive(context->lock);
}

static int blob_open(void *opaque, const char *path, int flags, int mode)
{
    (void)mode;
    readonly_blob_vfs_context_t *context = opaque;
    if (!take_lock(context)) {
        return -1;
    }
    const int result = readonly_blob_core_open(&context->core, path, flags);
    give_lock(context);
    return result;
}

static int blob_close(void *opaque, int fd)
{
    readonly_blob_vfs_context_t *context = opaque;
    if (!take_lock(context)) {
        return -1;
    }
    const int result = readonly_blob_core_close(&context->core, fd);
    give_lock(context);
    return result;
}

static ssize_t blob_read(void *opaque, int fd, void *destination, size_t size_bytes)
{
    readonly_blob_vfs_context_t *context = opaque;
    if (!take_lock(context)) {
        return -1;
    }
    const ssize_t result = readonly_blob_core_read(
        &context->core, fd, destination, size_bytes
    );
    give_lock(context);
    return result;
}

static ssize_t blob_pread(void *opaque,
                          int fd,
                          void *destination,
                          size_t size_bytes,
                          off_t offset)
{
    readonly_blob_vfs_context_t *context = opaque;
    if (!take_lock(context)) {
        return -1;
    }
    const ssize_t result = readonly_blob_core_pread(
        &context->core, fd, destination, size_bytes, offset
    );
    give_lock(context);
    return result;
}

static off_t blob_lseek(void *opaque, int fd, off_t offset, int whence)
{
    readonly_blob_vfs_context_t *context = opaque;
    if (!take_lock(context)) {
        return (off_t)-1;
    }
    const off_t result = readonly_blob_core_lseek(
        &context->core, fd, offset, whence
    );
    give_lock(context);
    return result;
}

static int blob_fstat(void *opaque, int fd, struct stat *metadata)
{
    readonly_blob_vfs_context_t *context = opaque;
    if (!take_lock(context)) {
        return -1;
    }
    const int result = readonly_blob_core_fstat(&context->core, fd, metadata);
    give_lock(context);
    return result;
}

#ifdef CONFIG_VFS_SUPPORT_DIR
static int blob_stat(void *opaque, const char *path, struct stat *metadata)
{
    readonly_blob_vfs_context_t *context = opaque;
    if (!take_lock(context)) {
        return -1;
    }
    const int result = readonly_blob_core_stat(&context->core, path, metadata);
    give_lock(context);
    return result;
}

static const esp_vfs_dir_ops_t s_blob_dir_operations = {
    .stat_p = blob_stat,
};
#endif

static const esp_vfs_fs_ops_t s_blob_operations = {
    .lseek_p = blob_lseek,
    .read_p = blob_read,
    .pread_p = blob_pread,
    .open_p = blob_open,
    .close_p = blob_close,
    .fstat_p = blob_fstat,
#ifdef CONFIG_VFS_SUPPORT_DIR
    .dir = &s_blob_dir_operations,
#endif
};

esp_err_t platform_readonly_blob_register(
    const platform_readonly_blob_config_t *config
)
{
    if (config == NULL || config->base_path == NULL || config->file_name == NULL
        || config->data == NULL || config->size_bytes == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t base_length = strlen(config->base_path);
    if (base_length < 2U || base_length > ESP_VFS_PATH_MAX
        || config->base_path[0] != '/'
        || config->base_path[base_length - 1U] == '/') {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_context.registered) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_context.lock == NULL) {
        s_context.lock = xSemaphoreCreateMutexStatic(&s_context.lock_storage);
        if (s_context.lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!readonly_blob_core_init(
            &s_context.core, config->data, config->size_bytes,
            config->file_name)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(s_context.base_path, config->base_path, base_length + 1U);
    const int flags = ESP_VFS_FLAG_CONTEXT_PTR | ESP_VFS_FLAG_READONLY_FS
        | ESP_VFS_FLAG_STATIC;
    const esp_err_t result = esp_vfs_register_fs(
        s_context.base_path, &s_blob_operations, flags, &s_context
    );
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
    if (readonly_blob_core_has_open_files(&s_context.core)) {
        give_lock(&s_context);
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = esp_vfs_unregister_fs(s_context.base_path);
    if (result == ESP_OK) {
        s_context.registered = false;
        memset(&s_context.core, 0, sizeof(s_context.core));
        memset(s_context.base_path, 0, sizeof(s_context.base_path));
    }
    give_lock(&s_context);
    return result;
}
