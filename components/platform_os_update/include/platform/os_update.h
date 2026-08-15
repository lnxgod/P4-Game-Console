// SPDX-License-Identifier: MIT

#ifndef P4_PLATFORM_OS_UPDATE_H
#define P4_PLATFORM_OS_UPDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "p4/os_update_package.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_OS_UPDATE_FILE_NAME "P4UPDATE.P4U"

enum {
    PLATFORM_OS_UPDATE_HEADER_BYTES = P4_OS_UPDATE_HEADER_BYTES,
    PLATFORM_OS_UPDATE_VERSION_BYTES = P4_OS_UPDATE_VERSION_BYTES,
    PLATFORM_OS_UPDATE_BUILD_BYTES = P4_OS_UPDATE_BUILD_BYTES,
    PLATFORM_OS_UPDATE_MAX_IMAGE_BYTES = P4_OS_UPDATE_MAX_IMAGE_BYTES,
    PLATFORM_OS_UPDATE_MAX_PACKAGE_BYTES = P4_OS_UPDATE_MAX_PACKAGE_BYTES,
};

typedef enum {
    PLATFORM_OS_UPDATE_ABSENT = 0,
    PLATFORM_OS_UPDATE_READY,
    PLATFORM_OS_UPDATE_INVALID,
    PLATFORM_OS_UPDATE_UNAVAILABLE,
} platform_os_update_state_t;

typedef struct {
    platform_os_update_state_t state;
    char version[PLATFORM_OS_UPDATE_VERSION_BYTES];
    char build[PLATFORM_OS_UPDATE_BUILD_BYTES];
    uint32_t image_bytes;
    uint8_t image_sha256[32];
    esp_err_t last_error;
} platform_os_update_info_t;

esp_err_t platform_os_update_inspect(platform_os_update_info_t *out_info);

/** Verify again, write only the inactive OTA slot, and select it atomically. */
esp_err_t platform_os_update_install(
    const platform_os_update_info_t *expected_info);

/** Confirm a pending image only after the Console OS reaches its ready frame. */
esp_err_t platform_os_update_mark_running_valid(bool *out_was_pending);

#ifdef __cplusplus
}
#endif

#endif
