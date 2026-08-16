// SPDX-License-Identifier: MIT

#ifndef P4_PLATFORM_GAME_STORAGE_TYPES_H
#define P4_PLATFORM_GAME_STORAGE_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PLATFORM_GAME_STORAGE_MAX_ROOT_ENTRIES = 32,
    PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES = 256,
};

typedef struct {
    char name[PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES];
    uint64_t size_bytes;
    bool is_directory;
    bool is_hidden;
} platform_game_storage_file_entry_t;

typedef struct {
    platform_game_storage_file_entry_t
        entries[PLATFORM_GAME_STORAGE_MAX_ROOT_ENTRIES];
    size_t entry_count;
    uint32_t total_entries;
    uint32_t hidden_entries;
    uint32_t omitted_entries;
    uint32_t storage_generation;
    uint32_t mutation_count;
} platform_game_storage_file_listing_t;

#ifdef __cplusplus
}
#endif

#endif
