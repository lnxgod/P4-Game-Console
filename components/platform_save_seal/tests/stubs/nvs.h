// SPDX-License-Identifier: MIT

#ifndef P4_TEST_NVS_H
#define P4_TEST_NVS_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef uint32_t nvs_handle_t;
typedef enum {
    NVS_READONLY = 1,
    NVS_READWRITE = 2,
} nvs_open_mode_t;

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode,
                   nvs_handle_t *out_handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key,
                       void *out_value, size_t *length);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key,
                       const void *value, size_t length);
esp_err_t nvs_commit(nvs_handle_t handle);

#endif
