// SPDX-License-Identifier: MIT

#include "fake_esp.h"

#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "nvs.h"
#include "sha256.h"

enum {
    FAKE_BLOB_BYTES = 2048,
    FAKE_BLOB_SLOTS = 40,
    FAKE_NVS_KEY_BYTES = 16,
};

typedef struct {
    uint8_t data[FAKE_BLOB_BYTES];
    size_t bytes;
    bool present;
} fake_blob_t;

typedef struct {
    char key[FAKE_NVS_KEY_BYTES];
    fake_blob_t blob;
} fake_named_blob_t;

typedef struct {
    fake_named_blob_t blobs[FAKE_BLOB_SLOTS];
} fake_namespace_t;

static fake_namespace_t s_committed;
static fake_namespace_t s_pending;
static bool s_open;
static bool s_writable;
static unsigned s_commits;
static unsigned s_random_calls;

static fake_blob_t *blob_for(fake_namespace_t *state, const char *key,
                             bool create)
{
    if (state == NULL || key == NULL || key[0] == '\0' ||
        strlen(key) >= FAKE_NVS_KEY_BYTES) {
        return NULL;
    }
    for (size_t index = 0U; index < FAKE_BLOB_SLOTS; ++index) {
        if (strcmp(state->blobs[index].key, key) == 0) {
            return &state->blobs[index].blob;
        }
    }
    if (create) {
        for (size_t index = 0U; index < FAKE_BLOB_SLOTS; ++index) {
            if (state->blobs[index].key[0] == '\0') {
                memcpy(state->blobs[index].key, key, strlen(key) + 1U);
                return &state->blobs[index].blob;
            }
        }
    }
    return NULL;
}

void fake_nvs_reset(void)
{
    memset(&s_committed, 0, sizeof(s_committed));
    memset(&s_pending, 0, sizeof(s_pending));
    s_open = false;
    s_writable = false;
    s_commits = 0U;
    s_random_calls = 0U;
}

void fake_nvs_delete_committed(const char *key)
{
    fake_blob_t *const blob = blob_for(&s_committed, key, false);
    if (blob != NULL) {
        for (size_t index = 0U; index < FAKE_BLOB_SLOTS; ++index) {
            if (&s_committed.blobs[index].blob == blob) {
                memset(&s_committed.blobs[index], 0,
                       sizeof(s_committed.blobs[index]));
                break;
            }
        }
    }
}

unsigned fake_nvs_delete_committed_prefix(const char *prefix)
{
    if (prefix == NULL) {
        return 0U;
    }
    const size_t prefix_bytes = strlen(prefix);
    unsigned deleted = 0U;
    for (size_t index = 0U; index < FAKE_BLOB_SLOTS; ++index) {
        if (s_committed.blobs[index].key[0] != '\0' &&
            strncmp(s_committed.blobs[index].key,
                    prefix, prefix_bytes) == 0) {
            memset(&s_committed.blobs[index], 0,
                   sizeof(s_committed.blobs[index]));
            ++deleted;
        }
    }
    return deleted;
}

void fake_nvs_replace_committed(const char *key,
                                const void *data, size_t bytes)
{
    fake_blob_t *const blob = blob_for(&s_committed, key, true);
    if (blob == NULL || data == NULL || bytes > sizeof(blob->data)) {
        return;
    }
    memset(blob, 0, sizeof(*blob));
    memcpy(blob->data, data, bytes);
    blob->bytes = bytes;
    blob->present = true;
}

bool fake_nvs_committed_present(const char *key)
{
    const fake_blob_t *const blob = blob_for(
        &s_committed, key, false);
    return blob != NULL && blob->present;
}

size_t fake_nvs_committed_bytes(const char *key)
{
    const fake_blob_t *const blob = blob_for(
        &s_committed, key, false);
    return blob != NULL && blob->present ? blob->bytes : 0U;
}

unsigned fake_nvs_commit_count(void)
{
    return s_commits;
}

unsigned fake_random_call_count(void)
{
    return s_random_calls;
}

void esp_fill_random(void *buffer, size_t length)
{
    ++s_random_calls;
    uint8_t *const output = buffer;
    for (size_t index = 0U; index < length; ++index) {
        output[index] = (uint8_t)(UINT8_C(0x41) + (uint8_t)index);
    }
}

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode,
                   nvs_handle_t *out_handle)
{
    if (namespace_name == NULL || out_handle == NULL || s_open ||
        strcmp(namespace_name, "p4_save_seal") != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    s_pending = s_committed;
    s_open = true;
    s_writable = open_mode == NVS_READWRITE;
    *out_handle = 1U;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    (void)handle;
    s_open = false;
    s_writable = false;
    memset(&s_pending, 0, sizeof(s_pending));
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key,
                       void *out_value, size_t *length)
{
    if (!s_open || handle != 1U || key == NULL || length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const fake_blob_t *const blob = blob_for(&s_pending, key, false);
    if (blob == NULL || !blob->present) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (out_value == NULL || *length < blob->bytes) {
        *length = blob->bytes;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out_value, blob->data, blob->bytes);
    *length = blob->bytes;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key,
                       const void *value, size_t length)
{
    if (!s_open || !s_writable || handle != 1U || key == NULL ||
        value == NULL || length > FAKE_BLOB_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    fake_blob_t *const blob = blob_for(&s_pending, key, true);
    if (blob == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(blob, 0, sizeof(*blob));
    memcpy(blob->data, value, length);
    blob->bytes = length;
    blob->present = true;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    if (!s_open || !s_writable || handle != 1U) {
        return ESP_ERR_INVALID_ARG;
    }
    s_committed = s_pending;
    ++s_commits;
    return ESP_OK;
}

int mbedtls_sha256(const unsigned char *input, size_t input_length,
                   unsigned char output[32], int is224)
{
    if ((input == NULL && input_length != 0U) || output == NULL ||
        is224 != 0) {
        return -1;
    }
    p4_game_save_sha256_t hash;
    p4_game_save_sha256_init(&hash);
    p4_game_save_sha256_update(&hash, input, input_length);
    p4_game_save_sha256_finish(&hash, output);
    return 0;
}
