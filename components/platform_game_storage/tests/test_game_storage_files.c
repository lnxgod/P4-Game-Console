// SPDX-License-Identifier: MIT

#define _DARWIN_C_SOURCE
#define _XOPEN_SOURCE 700

#include "game_storage_files.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    TEST_PATH_BYTES = 512,
};

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static bool compose(char path[TEST_PATH_BYTES],
                    const char *root, const char *name)
{
    const int written = snprintf(path, TEST_PATH_BYTES, "%s/%s", root, name);
    return written > 0 && (size_t)written < TEST_PATH_BYTES;
}

static bool make_file(const char *root, const char *name, const char *contents)
{
    char path[TEST_PATH_BYTES];
    if (!compose(path, root, name)) {
        return false;
    }
    FILE *const file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    const size_t bytes = strlen(contents);
    const bool written = fwrite(contents, 1U, bytes, file) == bytes;
    return fclose(file) == 0 && written;
}

static bool remove_file(const char *root, const char *name)
{
    char path[TEST_PATH_BYTES];
    return compose(path, root, name) && unlink(path) == 0;
}

static const platform_game_storage_file_entry_t *find_entry(
    const platform_game_storage_file_listing_t *listing,
    const char *name)
{
    for (size_t i = 0U; i < listing->entry_count; ++i) {
        if (strcmp(listing->entries[i].name, name) == 0) {
            return &listing->entries[i];
        }
    }
    return NULL;
}

static void test_name_validation(void)
{
    CHECK(!game_storage_files_root_name_valid(NULL));
    CHECK(!game_storage_files_root_name_valid(""));
    CHECK(!game_storage_files_root_name_valid("."));
    CHECK(!game_storage_files_root_name_valid(".."));
    CHECK(!game_storage_files_root_name_valid("../DOOM1.WAD"));
    CHECK(!game_storage_files_root_name_valid("DIR/FILE.WAD"));
    CHECK(!game_storage_files_root_name_valid("DIR\\FILE.WAD"));
    CHECK(!game_storage_files_root_name_valid("BAD\nNAME"));
    CHECK(game_storage_files_root_name_valid("DOOM1.WAD"));
    CHECK(game_storage_files_root_name_valid(".fseventsd"));
    CHECK(game_storage_files_relative_path_valid(""));
    CHECK(game_storage_files_relative_path_valid("GAMES/ARCADE"));
    CHECK(!game_storage_files_relative_path_valid(NULL));
    CHECK(!game_storage_files_relative_path_valid("/GAMES"));
    CHECK(!game_storage_files_relative_path_valid("GAMES/"));
    CHECK(!game_storage_files_relative_path_valid("GAMES//ARCADE"));
    CHECK(!game_storage_files_relative_path_valid("GAMES/../SAVES"));
    CHECK(!game_storage_files_relative_path_valid("GAMES\\ARCADE"));

    char unterminated[PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES];
    memset(unterminated, 'A', sizeof(unterminated));
    CHECK(!game_storage_files_root_name_valid(unterminated));
}

static void test_listing_and_removal(void)
{
    char temporary[] = "/tmp/p4-game-storage-XXXXXX";
    char *const root = mkdtemp(temporary);
    CHECK(root != NULL);
    if (root == NULL) {
        return;
    }

    CHECK(make_file(root, "B.WAD", "BBBB"));
    CHECK(make_file(root, "A.TXT", "AAA"));
    CHECK(make_file(root, ".hidden", "H"));
    char folder[TEST_PATH_BYTES];
    CHECK(compose(folder, root, "FOLDER"));
    CHECK(mkdir(folder, 0700) == 0);
    CHECK(make_file(folder, "NESTED.TXT", "inside"));

    platform_game_storage_file_listing_t listing;
    CHECK(game_storage_files_list_root(root, &listing) == 0);
    CHECK(listing.total_entries == 4U);
    CHECK(listing.entry_count == 3U);
    CHECK(listing.hidden_entries == 1U);
    CHECK(listing.omitted_entries == 0U);
    CHECK(strcmp(listing.entries[0].name, "FOLDER") == 0);
    CHECK(strcmp(listing.entries[1].name, "A.TXT") == 0);
    CHECK(strcmp(listing.entries[2].name, "B.WAD") == 0);
    const platform_game_storage_file_entry_t *const a =
        find_entry(&listing, "A.TXT");
    const platform_game_storage_file_entry_t *const directory =
        find_entry(&listing, "FOLDER");
    CHECK(a != NULL && a->size_bytes == 3U && !a->is_directory);
    CHECK(find_entry(&listing, ".hidden") == NULL);
    CHECK(directory != NULL && directory->is_directory &&
          directory->size_bytes == 0U);

    CHECK(game_storage_files_list_directory(
              root, "FOLDER", &listing) == 0);
    CHECK(listing.total_entries == 1U);
    CHECK(listing.entry_count == 1U);
    CHECK(strcmp(listing.entries[0].name, "NESTED.TXT") == 0);
    CHECK(listing.entries[0].size_bytes == 6U);
    CHECK(game_storage_files_list_directory(
              root, "../FOLDER", &listing) == EINVAL);
    CHECK(game_storage_files_remove_file(
              root, "FOLDER", "../A.TXT") == EINVAL);
    CHECK(game_storage_files_remove_file(
              root, "FOLDER", "NESTED.TXT") == 0);

    CHECK(game_storage_files_remove_root_file(root, "../A.TXT") == EINVAL);
    CHECK(game_storage_files_remove_root_file(root, "FOLDER") == EISDIR);
    CHECK(game_storage_files_remove_root_file(root, "A.TXT") == 0);
    CHECK(game_storage_files_remove_root_file(root, "A.TXT") == ENOENT);

    for (unsigned i = 0U;
         i < PLATFORM_GAME_STORAGE_MAX_ROOT_ENTRIES + 3U; ++i) {
        char name[16];
        const int written = snprintf(name, sizeof(name), "F%02u.BIN", i);
        CHECK(written > 0 && (size_t)written < sizeof(name));
        CHECK(make_file(root, name, "X"));
    }
    CHECK(game_storage_files_list_root(root, &listing) == 0);
    CHECK(listing.total_entries ==
          PLATFORM_GAME_STORAGE_MAX_ROOT_ENTRIES + 6U);
    CHECK(listing.entry_count == PLATFORM_GAME_STORAGE_MAX_ROOT_ENTRIES);
    CHECK(listing.hidden_entries == 1U);
    CHECK(listing.omitted_entries == 5U);
    for (size_t i = 1U; i < listing.entry_count; ++i) {
        const platform_game_storage_file_entry_t *const previous =
            &listing.entries[i - 1U];
        const platform_game_storage_file_entry_t *const current =
            &listing.entries[i];
        CHECK((previous->is_directory && !current->is_directory) ||
              (previous->is_directory == current->is_directory &&
               strcmp(previous->name, current->name) < 0));
    }

    for (unsigned i = 0U;
         i < PLATFORM_GAME_STORAGE_MAX_ROOT_ENTRIES + 3U; ++i) {
        char name[16];
        const int written = snprintf(name, sizeof(name), "F%02u.BIN", i);
        CHECK(written > 0 && (size_t)written < sizeof(name));
        CHECK(remove_file(root, name));
    }
    CHECK(remove_file(root, "B.WAD"));
    CHECK(remove_file(root, ".hidden"));
    CHECK(rmdir(folder) == 0);
    CHECK(rmdir(root) == 0);
}

int main(void)
{
    test_name_validation();
    test_listing_and_removal();
    if (s_failures != 0) {
        fprintf(stderr, "%d game storage file test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("platform game storage file tests passed");
    return EXIT_SUCCESS;
}
