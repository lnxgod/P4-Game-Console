#!/usr/bin/env python3
"""Exercise the complete production catalog scan with controlled I/O boundaries.

The real public structs and production catalog source are compiled with ASan,
UBSan, and a 2 KiB frame ceiling. Storage/package/crypto fixtures test listing
ownership, failure cleanup, and canonical GAMES-before-root duplicate handling.
The frame ceiling catches the 0.74 foreground refresh's 17 KiB local listings.
Set P4_CATALOG_SOURCE to a preserved source file for a negative baseline run.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(os.environ.get(
    "P4_CATALOG_SOURCE", ROOT / "components/platform_game_catalog/src/platform_game_catalog.c"))

FIXTURE = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *fixture_malloc(size_t bytes);
static void fixture_free(void *pointer);
#define malloc fixture_malloc
#define free fixture_free
#include "CATALOG_SOURCE"
#undef malloc
#undef free

static platform_game_catalog_t catalog, nested_catalog;
static void *live[2];
static unsigned allocations, releases, outstanding, peak, root_calls, games_calls, loads;
static esp_err_t root_result = ESP_OK, games_result = ESP_OK;
static bool allocation_fails, nested_scan, inside_nested, bad_digest;

static void *fixture_malloc(size_t bytes)
{
    assert(bytes == 2U * sizeof(platform_game_storage_file_listing_t));
    ++allocations;
    if (allocation_fails) return NULL;
    assert(outstanding < 2U);
    void *const pointer = calloc(1U, bytes);
    assert(pointer != NULL);
    for (unsigned i=0; i<2; ++i) if (!live[i]) { live[i]=pointer; break; }
    ++outstanding;
    if (outstanding > peak) peak = outstanding;
    return pointer;
}
static void fixture_free(void *pointer)
{
    bool found = false;
    for (unsigned i=0; i<2; ++i) if (live[i] == pointer) {
        live[i] = NULL; found = true; break;
    }
    assert(found && outstanding != 0U);
    --outstanding; ++releases; free(pointer);
}
static void add_file(platform_game_storage_file_listing_t *files, const char *name)
{
    platform_game_storage_file_entry_t *const file = &files->entries[files->entry_count++];
    snprintf(file->name, sizeof(file->name), "%s", name);
    file->size_bytes = 1U;
}
esp_err_t platform_game_storage_list_root(platform_game_storage_file_listing_t *files)
{
    ++root_calls;
    memset(files, 0, sizeof(*files));
    if (root_result != ESP_OK) return root_result;
    files->storage_generation = inside_nested ? 44U : 43U;
    add_file(files, "DUP.P4G"); add_file(files, "ROOT.P4G");
    add_file(files, "IGNORE.TXT"); add_file(files, "DIRECTORY.P4G");
    files->entries[3].is_directory = true;
    if (nested_scan && !inside_nested) {
        inside_nested = true;
        assert(platform_game_catalog_scan(&nested_catalog) == ESP_OK);
        inside_nested = false;
        assert(nested_catalog.storage_generation == 44U);
        assert(files->storage_generation == 43U);
    }
    return ESP_OK;
}
esp_err_t platform_game_storage_list_games(platform_game_storage_file_listing_t *files)
{
    ++games_calls;
    memset(files, 0, sizeof(*files));
    if (games_result != ESP_OK) return games_result;
    add_file(files, "DUP.P4G"); add_file(files, "GAME.P4G");
    return ESP_OK;
}
static esp_err_t load_file(const char *name, size_t maximum, uint8_t **data, size_t *bytes)
{
    assert(maximum == P4_GAME_PACKAGE_MAX_BYTES);
    *data = calloc(1U, 1U); assert(*data != NULL); *bytes = 1U; ++loads;
    **data = strcmp(name, "DUP.P4G") == 0 ? 7U : strcmp(name, "GAME.P4G") == 0 ? 8U : 9U;
    return ESP_OK;
}
esp_err_t platform_game_storage_load_root_file(const char *name, size_t maximum,
    uint8_t **data, size_t *bytes) { return load_file(name, maximum, data, bytes); }
esp_err_t platform_game_storage_load_game_file(const char *name, size_t maximum,
    uint8_t **data, size_t *bytes) { return load_file(name, maximum, data, bytes); }
void platform_game_storage_release_file(uint8_t *data) { free(data); }
esp_err_t platform_game_storage_remove_root_file(const char *name) { (void)name; return ESP_OK; }
esp_err_t platform_game_storage_remove_game_file(const char *name) { (void)name; return ESP_OK; }
p4_game_package_result_t p4_game_package_parse(const uint8_t *data, size_t bytes,
    p4_game_package_info_t *info)
{
    assert(bytes == 1U); memset(info, 0, sizeof(*info));
    info->launcher_id = data[0]; info->payload_bytes = 1U;
    snprintf(info->id, sizeof(info->id), "game-%u", (unsigned)data[0]);
    return P4_GAME_PACKAGE_VALID;
}
bool p4_game_package_read_icon(const uint8_t *data, size_t bytes, p4_game_icon_t *icon)
{ (void)data; (void)bytes; (void)icon; return false; }
int mbedtls_sha256(const unsigned char *data, size_t bytes, unsigned char output[32], int is224)
{
    (void)data; assert(bytes == 1U && !is224); memset(output, bad_digest ? 1 : 0, 32U); return 0;
}
int main(int argc, char **argv)
{
    /* Keep the unchanged pre-fix source compilable through the frame check. */
    (void)fixture_malloc; (void)fixture_free;
    assert(argc == 2);
    const char *const scenario = argv[1];
    if (!strcmp(scenario, "invalid")) {
        assert(platform_game_catalog_scan(NULL) == ESP_ERR_INVALID_ARG);
        assert(!allocations && !root_calls && !games_calls); return 0;
    }
    if (!strcmp(scenario, "no-memory")) allocation_fails = true;
    else if (!strcmp(scenario, "root-error")) root_result = ESP_FAIL;
    else if (!strcmp(scenario, "games-error")) games_result = ESP_FAIL;
    else if (!strcmp(scenario, "no-games")) games_result = ESP_ERR_NOT_FOUND;
    else if (!strcmp(scenario, "nested")) nested_scan = true;
    else if (!strcmp(scenario, "bad-digest")) bad_digest = true;
    else assert(!strcmp(scenario, "success"));
    memset(&catalog, 0xa5, sizeof(catalog));
    const esp_err_t result = platform_game_catalog_scan(&catalog);
    assert(!outstanding && !live[0] && !live[1]);
    if (allocation_fails) {
        assert(result == ESP_ERR_NO_MEM && allocations == 1U && !releases);
        assert(!catalog.available && !catalog.entry_count && !root_calls && !games_calls);
    } else if (root_result != ESP_OK) {
        assert(result == root_result && !catalog.available && !catalog.entry_count);
        assert(root_calls == 1U && !games_calls && !loads && releases == 1U);
    } else if (games_result == ESP_FAIL) {
        assert(result == ESP_FAIL && catalog.available && !catalog.entry_count);
        assert(root_calls == 1U && games_calls == 1U && !loads && releases == 1U);
    } else {
        assert(result == ESP_OK && catalog.available && catalog.storage_generation == 43U);
        if (games_result == ESP_ERR_NOT_FOUND) {
            assert(catalog.entry_count == 2U && catalog.valid_count == 2U && loads == 2U);
            assert(!catalog.entries[0].in_games_directory);
        } else {
            assert(catalog.entry_count == 4U);
            assert(catalog.valid_count == (bad_digest ? 0U : 3U));
            assert(catalog.entries[0].in_games_directory && catalog.entries[1].in_games_directory);
            assert(!catalog.entries[2].in_games_directory && !catalog.entries[3].in_games_directory);
            assert(!catalog.entries[2].valid);
            assert(catalog.entries[2].validation == (bad_digest
                ? P4_GAME_PACKAGE_BAD_DIGEST : P4_GAME_PACKAGE_BAD_METADATA));
            assert(loads == (nested_scan ? 8U : 4U));
        }
        assert(allocations == (nested_scan ? 2U : 1U) && releases == allocations);
        assert(peak == (nested_scan ? 2U : 1U));
    }
    return 0;
}
'''


class CatalogScanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="p4-catalog-scan-")
        cls.addClassCleanup(cls.temp.cleanup)
        directory = Path(cls.temp.name)
        (directory / "mbedtls").mkdir()
        (directory / "mbedtls/sha256.h").write_text(
            "#pragma once\n#include <stddef.h>\n"
            "int mbedtls_sha256(const unsigned char *, size_t, unsigned char [32], int);\n")
        (directory / "esp_err.h").write_text(
            "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n" +
            "\n".join(f"#define {name} {i+1}" for i, name in enumerate((
                "ESP_ERR_INVALID_ARG", "ESP_ERR_INVALID_SIZE", "ESP_ERR_NO_MEM", "ESP_ERR_NOT_FOUND"))))
        (directory / "catalog.c").write_text(FIXTURE.replace("CATALOG_SOURCE", str(SOURCE)))
        cls.exe = directory / "catalog"
        command = [os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra",
                   "-Werror", "-I", str(directory)]
        for component in ("platform_game_catalog", "platform_game_storage", "p4_game_package", "p4_game_api"):
            command += ["-I", str(ROOT / "components" / component / "include")]
        # Compile the unmodified production translation unit separately with
        # no sanitizer: ASan's fake-stack heap would hide large local frames.
        frame_command = command + ["-Wframe-larger-than=2048", "-c", str(SOURCE),
                                   "-o", str(directory / "catalog-frame.o")]
        result = subprocess.run(frame_command, capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        runtime_command = command + ["-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                                     str(directory / "catalog.c"), "-o", str(cls.exe)]
        result = subprocess.run(runtime_command, capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def run_case(self, scenario):
        result = subprocess.run([self.exe, scenario], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_invalid_output_does_not_allocate(self): self.run_case("invalid")
    def test_no_memory_does_not_scan_or_publish(self): self.run_case("no-memory")
    def test_root_failure_releases_workspace(self): self.run_case("root-error")
    def test_games_failure_releases_workspace(self): self.run_case("games-error")
    def test_missing_games_keeps_legacy_root_packages(self): self.run_case("no-games")
    def test_games_wins_duplicates_and_all_storage_is_released(self): self.run_case("success")
    def test_nested_scans_have_independent_storage(self): self.run_case("nested")
    def test_invalid_packages_release_workspace(self): self.run_case("bad-digest")


if __name__ == "__main__":
    unittest.main()
