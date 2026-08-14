#ifndef PLATFORM_READONLY_BLOB_H
#define PLATFORM_READONLY_BLOB_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PLATFORM_READONLY_BLOB_MAX_OPEN_FILES = 8,
};

typedef struct {
    /** VFS mount point, for example "/doom"; maximum ESP_VFS_PATH_MAX. */
    const char *base_path;
    /** Single file name below the mount point; slashes are rejected. */
    const char *file_name;
    /** Immutable flash- or RAM-backed file bytes retained by the caller. */
    const uint8_t *data;
    size_t size_bytes;
} platform_readonly_blob_config_t;

/** Register one immutable regular file through ESP-IDF VFS. */
esp_err_t platform_readonly_blob_register(
    const platform_readonly_blob_config_t *config
);

/** Unregister the singleton mount; fails while a file descriptor is open. */
esp_err_t platform_readonly_blob_unregister(void);

#ifdef __cplusplus
}
#endif

#endif
