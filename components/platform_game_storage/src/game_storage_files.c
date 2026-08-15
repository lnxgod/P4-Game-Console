// SPDX-License-Identifier: MIT

#include "game_storage_files.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    GAME_STORAGE_ROOT_MAX_BYTES = 192,
    GAME_STORAGE_PATH_MAX_BYTES =
        GAME_STORAGE_ROOT_MAX_BYTES +
        PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES + 2,
};

static size_t bounded_length(const char *text, size_t limit)
{
    if (text == NULL) {
        return limit;
    }
    size_t length = 0U;
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

bool game_storage_files_root_name_valid(const char *name)
{
    const size_t length = bounded_length(
        name, PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES);
    if (length == 0U ||
        length >= PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES ||
        (length == 1U && name[0] == '.') ||
        (length == 2U && name[0] == '.' && name[1] == '.')) {
        return false;
    }
    for (size_t i = 0U; i < length; ++i) {
        const unsigned char byte = (unsigned char)name[i];
        if (byte == (unsigned char)'/' || byte == (unsigned char)'\\' ||
            byte < UINT8_C(0x20) || byte == UINT8_C(0x7f)) {
            return false;
        }
    }
    return true;
}

static int compose_path(const char *root, const char *name,
                        char path[GAME_STORAGE_PATH_MAX_BYTES])
{
    const size_t root_length = bounded_length(root, GAME_STORAGE_ROOT_MAX_BYTES);
    if (root_length == 0U || root_length >= GAME_STORAGE_ROOT_MAX_BYTES ||
        !game_storage_files_root_name_valid(name)) {
        return EINVAL;
    }
    const int written = snprintf(
        path, GAME_STORAGE_PATH_MAX_BYTES, "%s/%s", root, name);
    return written > 0 && (size_t)written < GAME_STORAGE_PATH_MAX_BYTES
        ? 0 : ENAMETOOLONG;
}

static void sort_listing(platform_game_storage_file_listing_t *listing)
{
    for (size_t i = 1U; i < listing->entry_count; ++i) {
        const platform_game_storage_file_entry_t value = listing->entries[i];
        size_t position = i;
        while (position > 0U &&
               strcmp(listing->entries[position - 1U].name,
                      value.name) > 0) {
            listing->entries[position] = listing->entries[position - 1U];
            --position;
        }
        listing->entries[position] = value;
    }
}

int game_storage_files_list_root(
    const char *root,
    platform_game_storage_file_listing_t *out_listing)
{
    if (root == NULL || out_listing == NULL) {
        return EINVAL;
    }
    memset(out_listing, 0, sizeof(*out_listing));
    if (bounded_length(root, GAME_STORAGE_ROOT_MAX_BYTES) == 0U ||
        bounded_length(root, GAME_STORAGE_ROOT_MAX_BYTES) >=
            GAME_STORAGE_ROOT_MAX_BYTES) {
        return EINVAL;
    }

    DIR *const directory = opendir(root);
    if (directory == NULL) {
        return errno != 0 ? errno : EIO;
    }

    int result = 0;
    for (;;) {
        errno = 0;
        const struct dirent *const item = readdir(directory);
        if (item == NULL) {
            if (errno != 0) {
                result = errno;
            }
            break;
        }
        if ((item->d_name[0] == '.' && item->d_name[1] == '\0') ||
            (item->d_name[0] == '.' && item->d_name[1] == '.' &&
             item->d_name[2] == '\0')) {
            continue;
        }
        if (out_listing->total_entries != UINT32_MAX) {
            ++out_listing->total_entries;
        }
        const bool hidden = item->d_name[0] == '.';
        if (hidden) {
            if (out_listing->hidden_entries != UINT32_MAX) {
                ++out_listing->hidden_entries;
            }
            continue;
        }
        if (!game_storage_files_root_name_valid(item->d_name) ||
            out_listing->entry_count >=
                PLATFORM_GAME_STORAGE_MAX_ROOT_ENTRIES) {
            if (out_listing->omitted_entries != UINT32_MAX) {
                ++out_listing->omitted_entries;
            }
            continue;
        }

        char path[GAME_STORAGE_PATH_MAX_BYTES];
        const int compose = compose_path(root, item->d_name, path);
        struct stat metadata;
        if (compose != 0 || stat(path, &metadata) != 0 ||
            metadata.st_size < 0) {
            if (out_listing->omitted_entries != UINT32_MAX) {
                ++out_listing->omitted_entries;
            }
            continue;
        }

        platform_game_storage_file_entry_t *const entry =
            &out_listing->entries[out_listing->entry_count++];
        const size_t name_length = bounded_length(
            item->d_name, PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES);
        memcpy(entry->name, item->d_name, name_length + 1U);
        entry->is_directory = S_ISDIR(metadata.st_mode);
        entry->is_hidden = false;
        entry->size_bytes = entry->is_directory
            ? UINT64_C(0) : (uint64_t)metadata.st_size;
    }
    if (closedir(directory) != 0 && result == 0) {
        result = errno != 0 ? errno : EIO;
    }
    if (result == 0) {
        sort_listing(out_listing);
    }
    return result;
}

int game_storage_files_remove_root_file(const char *root, const char *name)
{
    if (root == NULL || !game_storage_files_root_name_valid(name)) {
        return EINVAL;
    }
    char path[GAME_STORAGE_PATH_MAX_BYTES];
    const int compose = compose_path(root, name, path);
    if (compose != 0) {
        return compose;
    }
    struct stat metadata;
    if (stat(path, &metadata) != 0) {
        return errno != 0 ? errno : EIO;
    }
    if (!S_ISREG(metadata.st_mode)) {
        return EISDIR;
    }
    if (unlink(path) != 0) {
        return errno != 0 ? errno : EIO;
    }
    return 0;
}
