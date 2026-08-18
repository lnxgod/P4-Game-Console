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
        PLATFORM_GAME_STORAGE_RELATIVE_PATH_MAX_BYTES +
        PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES + 3,
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

bool game_storage_files_relative_path_valid(const char *relative_path)
{
    const size_t length = bounded_length(
        relative_path, PLATFORM_GAME_STORAGE_RELATIVE_PATH_MAX_BYTES);
    if (relative_path == NULL ||
        length >= PLATFORM_GAME_STORAGE_RELATIVE_PATH_MAX_BYTES) {
        return false;
    }
    if (length == 0U) {
        return true;
    }
    if (relative_path[0] == '/' || relative_path[length - 1U] == '/') {
        return false;
    }

    size_t segment_start = 0U;
    for (size_t index = 0U; index <= length; ++index) {
        if (index < length && relative_path[index] != '/') {
            const unsigned char byte = (unsigned char)relative_path[index];
            if (byte == (unsigned char)'\\' || byte < UINT8_C(0x20) ||
                byte == UINT8_C(0x7f)) {
                return false;
            }
            continue;
        }
        const size_t segment_length = index - segment_start;
        if (segment_length == 0U ||
            segment_length >= PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES ||
            (segment_length == 1U &&
             relative_path[segment_start] == '.') ||
            (segment_length == 2U &&
             relative_path[segment_start] == '.' &&
             relative_path[segment_start + 1U] == '.')) {
            return false;
        }
        segment_start = index + 1U;
    }
    return true;
}

static int compose_directory_path(
    const char *root, const char *relative_path,
    char path[GAME_STORAGE_PATH_MAX_BYTES])
{
    const size_t root_length = bounded_length(
        root, GAME_STORAGE_ROOT_MAX_BYTES);
    if (root_length == 0U || root_length >= GAME_STORAGE_ROOT_MAX_BYTES ||
        !game_storage_files_relative_path_valid(relative_path)) {
        return EINVAL;
    }
    const int written = relative_path[0] == '\0'
        ? snprintf(path, GAME_STORAGE_PATH_MAX_BYTES, "%s", root)
        : snprintf(path, GAME_STORAGE_PATH_MAX_BYTES, "%s/%s",
                   root, relative_path);
    return written > 0 && (size_t)written < GAME_STORAGE_PATH_MAX_BYTES
        ? 0 : ENAMETOOLONG;
}

static int compose_entry_path(
    const char *root, const char *relative_path, const char *name,
    char path[GAME_STORAGE_PATH_MAX_BYTES])
{
    char directory[GAME_STORAGE_PATH_MAX_BYTES];
    const int directory_result = compose_directory_path(
        root, relative_path, directory);
    if (directory_result != 0 ||
        !game_storage_files_root_name_valid(name)) {
        return directory_result != 0 ? directory_result : EINVAL;
    }
    const int written = snprintf(
        path, GAME_STORAGE_PATH_MAX_BYTES, "%s/%s", directory, name);
    return written > 0 && (size_t)written < GAME_STORAGE_PATH_MAX_BYTES
        ? 0 : ENAMETOOLONG;
}

static void sort_listing(platform_game_storage_file_listing_t *listing)
{
    for (size_t i = 1U; i < listing->entry_count; ++i) {
        const platform_game_storage_file_entry_t value = listing->entries[i];
        size_t position = i;
        while (position > 0U) {
            const platform_game_storage_file_entry_t *const previous =
                &listing->entries[position - 1U];
            const bool value_before_previous =
                (value.is_directory && !previous->is_directory) ||
                (value.is_directory == previous->is_directory &&
                 strcmp(previous->name, value.name) > 0);
            if (!value_before_previous) {
                break;
            }
            listing->entries[position] = listing->entries[position - 1U];
            --position;
        }
        listing->entries[position] = value;
    }
}

int game_storage_files_list_directory(
    const char *root, const char *relative_path,
    platform_game_storage_file_listing_t *out_listing)
{
    if (root == NULL || relative_path == NULL || out_listing == NULL) {
        return EINVAL;
    }
    memset(out_listing, 0, sizeof(*out_listing));
    char directory_path[GAME_STORAGE_PATH_MAX_BYTES];
    const int compose_directory = compose_directory_path(
        root, relative_path, directory_path);
    if (compose_directory != 0) {
        return compose_directory;
    }

    DIR *const directory = opendir(directory_path);
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
        const int compose = compose_entry_path(
            root, relative_path, item->d_name, path);
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

int game_storage_files_list_root(
    const char *root,
    platform_game_storage_file_listing_t *out_listing)
{
    return game_storage_files_list_directory(root, "", out_listing);
}

int game_storage_files_remove_file(
    const char *root, const char *relative_path, const char *name)
{
    if (root == NULL || relative_path == NULL ||
        !game_storage_files_relative_path_valid(relative_path) ||
        !game_storage_files_root_name_valid(name)) {
        return EINVAL;
    }
    char path[GAME_STORAGE_PATH_MAX_BYTES];
    const int compose = compose_entry_path(root, relative_path, name, path);
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

int game_storage_files_remove_root_file(const char *root, const char *name)
{
    return game_storage_files_remove_file(root, "", name);
}
