// SPDX-License-Identifier: MIT

#ifndef P4_TEST_FAKE_ESP_H
#define P4_TEST_FAKE_ESP_H

#include <stdbool.h>
#include <stddef.h>

void fake_nvs_reset(void);
void fake_nvs_delete_committed(const char *key);
unsigned fake_nvs_delete_committed_prefix(const char *prefix);
void fake_nvs_replace_committed(const char *key,
                                const void *data, size_t bytes);
bool fake_nvs_committed_present(const char *key);
size_t fake_nvs_committed_bytes(const char *key);
unsigned fake_nvs_commit_count(void);
unsigned fake_random_call_count(void);

#endif
