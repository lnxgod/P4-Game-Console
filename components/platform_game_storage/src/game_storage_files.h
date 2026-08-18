// SPDX-License-Identifier: MIT

#ifndef P4_GAME_STORAGE_FILES_H
#define P4_GAME_STORAGE_FILES_H

#include <stdbool.h>

#include "platform/game_storage_types.h"

bool game_storage_files_root_name_valid(const char *name);

/** Empty means the root; otherwise only slash-separated safe names pass. */
bool game_storage_files_relative_path_valid(const char *relative_path);

/** Return zero on success or a positive errno value on failure. */
int game_storage_files_list_directory(
    const char *root, const char *relative_path,
    platform_game_storage_file_listing_t *out_listing);

/** Return zero on success or a positive errno value on failure. */
int game_storage_files_list_root(
    const char *root,
    platform_game_storage_file_listing_t *out_listing);

/** Return zero on success or a positive errno value on failure. */
int game_storage_files_remove_root_file(const char *root, const char *name);

/** Return zero on success or a positive errno value on failure. */
int game_storage_files_remove_file(
    const char *root, const char *relative_path, const char *name);

#endif
