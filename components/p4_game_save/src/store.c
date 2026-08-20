// SPDX-License-Identifier: MIT

#define _POSIX_C_SOURCE 200809L

#include "p4/game_save_store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    JOURNAL_BYTES = 128,
    JOURNAL_MAGIC = 0,
    JOURNAL_SIZE = 8,
    JOURNAL_VERSION = 12,
    JOURNAL_STATE = 16,
    JOURNAL_EXPECTED_SEQUENCE = 20,
    JOURNAL_TARGET_SEQUENCE = 24,
    JOURNAL_GAME_ID = 28,
    JOURNAL_SLOT_ID = 76,
    JOURNAL_OBJECT_SHA256 = 92,
    JOURNAL_CRC32 = 124,
    JOURNAL_FORMAT_VERSION = 1,
};

typedef enum {
    JOURNAL_STAGED = 1,
    JOURNAL_BACKED_UP,
    JOURNAL_INSTALLED,
} journal_state_t;

typedef struct {
    journal_state_t state;
    uint32_t expected_sequence;
    uint32_t target_sequence;
    char game_id[P4_GAME_ID_MAX_BYTES];
    char slot_id[P4_GAME_SAVE_SLOT_ID_BYTES];
    uint8_t object_sha256[P4_GAME_SAVE_SHA256_BYTES];
} journal_t;

typedef struct {
    char directory[P4_GAME_SAVE_STORE_PATH_BYTES];
    char current[P4_GAME_SAVE_STORE_PATH_BYTES];
    char stage[P4_GAME_SAVE_STORE_PATH_BYTES];
    char backup[P4_GAME_SAVE_STORE_PATH_BYTES];
    char journal[P4_GAME_SAVE_STORE_PATH_BYTES];
} save_paths_t;

typedef struct {
    p4_game_save_store_result_t result;
    p4_game_save_info_t info;
    size_t bytes;
} object_file_t;

static const uint8_t s_journal_magic[8] = {
    'P', '4', 'J', 'R', 'N', '1', 0, 0,
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

static bool root_path_valid(const char *path)
{
    const size_t length = bounded_length(path, P4_GAME_SAVE_STORE_ROOT_BYTES);
    if (length == 0U || length >= P4_GAME_SAVE_STORE_ROOT_BYTES ||
        path[0] != '/') {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char character = (unsigned char)path[index];
        if (character < UINT8_C(0x20) || character > UINT8_C(0x7e)) {
            return false;
        }
    }
    return strstr(path, "/../") == NULL &&
        strstr(path, "/./") == NULL && strstr(path, "//") == NULL;
}

static bool mode_valid(p4_game_save_storage_mode_t mode)
{
    return mode >= P4_GAME_SAVE_STORAGE_UNAVAILABLE &&
        mode <= P4_GAME_SAVE_STORAGE_HOST_OWNED;
}

bool p4_game_save_store_init(p4_game_save_store_t *store,
                             const char *root_path,
                             p4_game_save_storage_mode_t mode)
{
    if (store == NULL || !root_path_valid(root_path) || !mode_valid(mode)) {
        return false;
    }
    memset(store, 0, sizeof(*store));
    memcpy(store->root_path, root_path, strlen(root_path) + 1U);
    store->mode = mode;
    store->initialized = true;
    return true;
}

void p4_game_save_store_set_mode(p4_game_save_store_t *store,
                                 p4_game_save_storage_mode_t mode)
{
    if (store != NULL && store->initialized && mode_valid(mode)) {
        store->mode = mode;
    }
}

void p4_game_save_store_set_transition_hook(
    p4_game_save_store_t *store,
    p4_game_save_transition_hook_fn hook,
    void *context)
{
    if (store != NULL && store->initialized) {
        store->transition_hook = hook;
        store->transition_context = context;
    }
}

static bool transition_allowed(p4_game_save_store_t *store,
                               p4_game_save_transition_t transition)
{
    return store->transition_hook == NULL ||
        store->transition_hook(store->transition_context, transition);
}

static bool compose_paths(const p4_game_save_store_t *store,
                          const char *game_id,
                          const char *slot_id,
                          save_paths_t *paths)
{
    if (store == NULL || !store->initialized || paths == NULL ||
        !p4_game_save_game_id_valid(game_id) ||
        !p4_game_save_slot_id_valid(slot_id)) {
        return false;
    }
    memset(paths, 0, sizeof(*paths));
    const int directory = snprintf(
        paths->directory, sizeof(paths->directory), "%s/SAVES/%s",
        store->root_path, game_id);
    const int current = snprintf(
        paths->current, sizeof(paths->current), "%s/%s.P4SAVE",
        paths->directory, slot_id);
    const int stage = snprintf(
        paths->stage, sizeof(paths->stage), "%s/%s.P4SAVE.STAGE",
        paths->directory, slot_id);
    const int backup = snprintf(
        paths->backup, sizeof(paths->backup), "%s/%s.P4SAVE.BAK",
        paths->directory, slot_id);
    const int journal = snprintf(
        paths->journal, sizeof(paths->journal), "%s/%s.P4SAVE.JOURNAL",
        paths->directory, slot_id);
    return directory > 0 && (size_t)directory < sizeof(paths->directory) &&
        current > 0 && (size_t)current < sizeof(paths->current) &&
        stage > 0 && (size_t)stage < sizeof(paths->stage) &&
        backup > 0 && (size_t)backup < sizeof(paths->backup) &&
        journal > 0 && (size_t)journal < sizeof(paths->journal);
}

static bool directory_present(const char *path)
{
    struct stat metadata;
    return stat(path, &metadata) == 0 && S_ISDIR(metadata.st_mode);
}

static bool ensure_directory(const char *path)
{
    if (mkdir(path, 0775) == 0) {
        return true;
    }
    return errno == EEXIST && directory_present(path);
}

static bool ensure_save_directories(const p4_game_save_store_t *store,
                                    const save_paths_t *paths)
{
    char saves[P4_GAME_SAVE_STORE_PATH_BYTES];
    const int length = snprintf(
        saves, sizeof(saves), "%s/SAVES", store->root_path);
    return directory_present(store->root_path) && length > 0 &&
        (size_t)length < sizeof(saves) && ensure_directory(saves) &&
        ensure_directory(paths->directory);
}

static bool write_synced(const char *path, const uint8_t *data, size_t bytes)
{
    FILE *const file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    bool okay = fwrite(data, 1U, bytes, file) == bytes &&
        fflush(file) == 0;
    if (okay) {
        const int descriptor = fileno(file);
        okay = descriptor >= 0 && fsync(descriptor) == 0;
    }
    if (fclose(file) != 0) {
        okay = false;
    }
    return okay;
}

static bool read_exact(const char *path, uint8_t *data,
                       size_t capacity, size_t *bytes_out)
{
    if (bytes_out != NULL) {
        *bytes_out = 0U;
    }
    struct stat metadata;
    if (data == NULL || bytes_out == NULL || stat(path, &metadata) != 0 ||
        !S_ISREG(metadata.st_mode) || metadata.st_size < 0 ||
        (uint64_t)metadata.st_size > capacity) {
        return false;
    }
    const size_t bytes = (size_t)metadata.st_size;
    FILE *const file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    bool okay = fread(data, 1U, bytes, file) == bytes;
    if (okay) {
        okay = fgetc(file) == EOF && ferror(file) == 0;
    }
    if (fclose(file) != 0) {
        okay = false;
    }
    if (okay) {
        *bytes_out = bytes;
    }
    return okay;
}

static bool file_exists(const char *path)
{
    struct stat metadata;
    return stat(path, &metadata) == 0;
}

static bool remove_if_present(const char *path)
{
    return unlink(path) == 0 || errno == ENOENT;
}

static object_file_t read_object(
    const char *path,
    const char *game_id,
    const char *slot_id,
    uint8_t *workspace,
    size_t workspace_bytes)
{
    object_file_t object = {
        .result = P4_GAME_SAVE_STORE_NOT_FOUND,
    };
    if (!file_exists(path)) {
        return object;
    }
    if (!read_exact(path, workspace, workspace_bytes, &object.bytes)) {
        object.result = P4_GAME_SAVE_STORE_IO_ERROR;
        return object;
    }
    object.result = p4_game_save_parse(
        workspace, object.bytes, game_id, slot_id, &object.info) ==
            P4_GAME_SAVE_VALID
        ? P4_GAME_SAVE_STORE_OK : P4_GAME_SAVE_STORE_CORRUPT;
    return object;
}

static void write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
        ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static uint32_t crc32_journal(const uint8_t data[JOURNAL_BYTES])
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t index = 0U; index < JOURNAL_BYTES; ++index) {
        const uint8_t value = index >= JOURNAL_CRC32 ? 0U : data[index];
        crc ^= value;
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}

static bool fixed_text_valid(const uint8_t *field, size_t field_bytes,
                             char *output, size_t output_bytes)
{
    const uint8_t *const terminator = memchr(field, 0, field_bytes);
    if (terminator == NULL || output_bytes != field_bytes) {
        return false;
    }
    const size_t length = (size_t)(terminator - field);
    for (size_t index = length + 1U; index < field_bytes; ++index) {
        if (field[index] != 0U) {
            return false;
        }
    }
    memcpy(output, field, field_bytes);
    return true;
}

static bool journal_decode(const uint8_t data[JOURNAL_BYTES],
                           const char *game_id,
                           const char *slot_id,
                           journal_t *journal)
{
    if (memcmp(data + JOURNAL_MAGIC, s_journal_magic,
               sizeof(s_journal_magic)) != 0 ||
        read_u32(data + JOURNAL_SIZE) != JOURNAL_BYTES ||
        read_u32(data + JOURNAL_VERSION) != JOURNAL_FORMAT_VERSION ||
        read_u32(data + JOURNAL_CRC32) != crc32_journal(data)) {
        return false;
    }
    const uint32_t state = read_u32(data + JOURNAL_STATE);
    if (state < JOURNAL_STAGED || state > JOURNAL_INSTALLED) {
        return false;
    }
    memset(journal, 0, sizeof(*journal));
    journal->state = (journal_state_t)state;
    journal->expected_sequence =
        read_u32(data + JOURNAL_EXPECTED_SEQUENCE);
    journal->target_sequence = read_u32(data + JOURNAL_TARGET_SEQUENCE);
    if (journal->target_sequence == 0U ||
        journal->target_sequence != journal->expected_sequence + 1U ||
        !fixed_text_valid(data + JOURNAL_GAME_ID, P4_GAME_ID_MAX_BYTES,
                          journal->game_id, sizeof(journal->game_id)) ||
        !fixed_text_valid(data + JOURNAL_SLOT_ID,
                          P4_GAME_SAVE_SLOT_ID_BYTES,
                          journal->slot_id, sizeof(journal->slot_id)) ||
        strcmp(journal->game_id, game_id) != 0 ||
        strcmp(journal->slot_id, slot_id) != 0) {
        return false;
    }
    memcpy(journal->object_sha256, data + JOURNAL_OBJECT_SHA256,
           sizeof(journal->object_sha256));
    return true;
}

static bool read_journal(const char *path,
                         const char *game_id,
                         const char *slot_id,
                         journal_t *journal)
{
    uint8_t data[JOURNAL_BYTES];
    size_t bytes = 0U;
    return read_exact(path, data, sizeof(data), &bytes) &&
        bytes == sizeof(data) &&
        journal_decode(data, game_id, slot_id, journal);
}

static bool write_journal(const char *path, const journal_t *journal,
                          journal_state_t state)
{
    uint8_t data[JOURNAL_BYTES] = {0};
    memcpy(data + JOURNAL_MAGIC, s_journal_magic, sizeof(s_journal_magic));
    write_u32(data + JOURNAL_SIZE, JOURNAL_BYTES);
    write_u32(data + JOURNAL_VERSION, JOURNAL_FORMAT_VERSION);
    write_u32(data + JOURNAL_STATE, (uint32_t)state);
    write_u32(data + JOURNAL_EXPECTED_SEQUENCE,
              journal->expected_sequence);
    write_u32(data + JOURNAL_TARGET_SEQUENCE, journal->target_sequence);
    memcpy(data + JOURNAL_GAME_ID, journal->game_id,
           strlen(journal->game_id));
    memcpy(data + JOURNAL_SLOT_ID, journal->slot_id,
           strlen(journal->slot_id));
    memcpy(data + JOURNAL_OBJECT_SHA256, journal->object_sha256,
           sizeof(journal->object_sha256));
    write_u32(data + JOURNAL_CRC32, crc32_journal(data));
    if (!write_synced(path, data, sizeof(data))) {
        return false;
    }
    journal_t readback;
    return read_journal(path, journal->game_id, journal->slot_id, &readback) &&
        readback.state == state &&
        readback.expected_sequence == journal->expected_sequence &&
        readback.target_sequence == journal->target_sequence &&
        memcmp(readback.object_sha256, journal->object_sha256,
               sizeof(readback.object_sha256)) == 0;
}

static bool object_matches(const object_file_t *object,
                           const journal_t *journal,
                           uint32_t sequence)
{
    return object->result == P4_GAME_SAVE_STORE_OK &&
        object->info.sequence == sequence &&
        memcmp(object->info.object_sha256, journal->object_sha256,
               sizeof(object->info.object_sha256)) == 0;
}

static p4_game_save_store_result_t fallback_recovery(
    const save_paths_t *paths,
    const char *game_id,
    const char *slot_id,
    uint8_t *workspace,
    size_t workspace_bytes)
{
    object_file_t current = read_object(
        paths->current, game_id, slot_id, workspace, workspace_bytes);
    if (current.result == P4_GAME_SAVE_STORE_OK) {
        if (!remove_if_present(paths->stage) ||
            !remove_if_present(paths->journal)) {
            return P4_GAME_SAVE_STORE_IO_ERROR;
        }
        return P4_GAME_SAVE_STORE_OK;
    }
    object_file_t backup = read_object(
        paths->backup, game_id, slot_id, workspace, workspace_bytes);
    if (backup.result == P4_GAME_SAVE_STORE_OK) {
        if (!remove_if_present(paths->current) ||
            rename(paths->backup, paths->current) != 0) {
            return P4_GAME_SAVE_STORE_IO_ERROR;
        }
        current = read_object(
            paths->current, game_id, slot_id, workspace, workspace_bytes);
        if (current.result != P4_GAME_SAVE_STORE_OK ||
            !remove_if_present(paths->stage) ||
            !remove_if_present(paths->journal)) {
            return current.result == P4_GAME_SAVE_STORE_OK
                ? P4_GAME_SAVE_STORE_IO_ERROR : current.result;
        }
        return P4_GAME_SAVE_STORE_OK;
    }
    if (current.result == P4_GAME_SAVE_STORE_NOT_FOUND &&
        backup.result == P4_GAME_SAVE_STORE_NOT_FOUND) {
        return P4_GAME_SAVE_STORE_NOT_FOUND;
    }
    return P4_GAME_SAVE_STORE_CORRUPT;
}

static p4_game_save_store_result_t resume_journal(
    p4_game_save_store_t *store,
    const save_paths_t *paths,
    const journal_t *journal,
    const char *game_id,
    const char *slot_id,
    uint8_t *workspace,
    size_t workspace_bytes)
{
    object_file_t current = read_object(
        paths->current, game_id, slot_id, workspace, workspace_bytes);
    if (object_matches(&current, journal, journal->target_sequence)) {
        goto installed;
    }
    object_file_t stage = read_object(
        paths->stage, game_id, slot_id, workspace, workspace_bytes);
    object_file_t backup = read_object(
        paths->backup, game_id, slot_id, workspace, workspace_bytes);
    if (!object_matches(&stage, journal, journal->target_sequence)) {
        return fallback_recovery(
            paths, game_id, slot_id, workspace, workspace_bytes);
    }
    const bool current_expected = journal->expected_sequence != 0U &&
        current.result == P4_GAME_SAVE_STORE_OK &&
        current.info.sequence == journal->expected_sequence;
    const bool backup_expected = journal->expected_sequence != 0U &&
        backup.result == P4_GAME_SAVE_STORE_OK &&
        backup.info.sequence == journal->expected_sequence;
    const bool no_predecessor = journal->expected_sequence == 0U &&
        current.result == P4_GAME_SAVE_STORE_NOT_FOUND;
    if (!current_expected && !backup_expected && !no_predecessor) {
        return fallback_recovery(
            paths, game_id, slot_id, workspace, workspace_bytes);
    }
    if (current_expected) {
        if (!remove_if_present(paths->backup) ||
            rename(paths->current, paths->backup) != 0) {
            return P4_GAME_SAVE_STORE_IO_ERROR;
        }
        if (!transition_allowed(
                store, P4_GAME_SAVE_TRANSITION_BACKUP_INSTALLED)) {
            return P4_GAME_SAVE_STORE_INTERRUPTED;
        }
    }
    if (journal->state < JOURNAL_BACKED_UP || current_expected) {
        if (!write_journal(paths->journal, journal, JOURNAL_BACKED_UP)) {
            return P4_GAME_SAVE_STORE_IO_ERROR;
        }
        if (!transition_allowed(
                store, P4_GAME_SAVE_TRANSITION_JOURNAL_BACKED_UP)) {
            return P4_GAME_SAVE_STORE_INTERRUPTED;
        }
    }
    if (file_exists(paths->current) && !remove_if_present(paths->current)) {
        return P4_GAME_SAVE_STORE_IO_ERROR;
    }
    if (rename(paths->stage, paths->current) != 0) {
        return P4_GAME_SAVE_STORE_IO_ERROR;
    }
    if (!transition_allowed(
            store, P4_GAME_SAVE_TRANSITION_CURRENT_INSTALLED)) {
        return P4_GAME_SAVE_STORE_INTERRUPTED;
    }

installed:
    if (!write_journal(paths->journal, journal, JOURNAL_INSTALLED)) {
        return P4_GAME_SAVE_STORE_IO_ERROR;
    }
    if (!transition_allowed(
            store, P4_GAME_SAVE_TRANSITION_JOURNAL_INSTALLED)) {
        return P4_GAME_SAVE_STORE_INTERRUPTED;
    }
    current = read_object(
        paths->current, game_id, slot_id, workspace, workspace_bytes);
    if (!object_matches(&current, journal, journal->target_sequence)) {
        return fallback_recovery(
            paths, game_id, slot_id, workspace, workspace_bytes);
    }
    if (!transition_allowed(
            store, P4_GAME_SAVE_TRANSITION_CURRENT_VERIFIED)) {
        return P4_GAME_SAVE_STORE_INTERRUPTED;
    }
    if (!remove_if_present(paths->stage) ||
        !remove_if_present(paths->journal)) {
        return P4_GAME_SAVE_STORE_IO_ERROR;
    }
    if (!transition_allowed(
            store, P4_GAME_SAVE_TRANSITION_JOURNAL_REMOVED)) {
        return P4_GAME_SAVE_STORE_INTERRUPTED;
    }
    return P4_GAME_SAVE_STORE_OK;
}

static p4_game_save_store_result_t mode_for_read(
    const p4_game_save_store_t *store)
{
    if (store == NULL || !store->initialized) {
        return P4_GAME_SAVE_STORE_BAD_ARGUMENT;
    }
    return store->mode == P4_GAME_SAVE_STORAGE_UNAVAILABLE ||
           store->mode == P4_GAME_SAVE_STORAGE_HOST_OWNED
        ? P4_GAME_SAVE_STORE_UNAVAILABLE : P4_GAME_SAVE_STORE_OK;
}

p4_game_save_store_result_t p4_game_save_store_recover(
    p4_game_save_store_t *store,
    const char *game_id,
    const char *slot_id,
    uint8_t *object_workspace,
    size_t object_workspace_bytes)
{
    if (mode_for_read(store) != P4_GAME_SAVE_STORE_OK ||
        object_workspace == NULL ||
        object_workspace_bytes < P4_GAME_SAVE_MAX_FILE_BYTES) {
        return mode_for_read(store) == P4_GAME_SAVE_STORE_OK
            ? P4_GAME_SAVE_STORE_BAD_ARGUMENT : mode_for_read(store);
    }
    if (store->mode != P4_GAME_SAVE_STORAGE_WRITABLE) {
        return P4_GAME_SAVE_STORE_READ_ONLY;
    }
    save_paths_t paths;
    if (!compose_paths(store, game_id, slot_id, &paths)) {
        return P4_GAME_SAVE_STORE_BAD_ARGUMENT;
    }
    if (!directory_present(paths.directory)) {
        return P4_GAME_SAVE_STORE_NOT_FOUND;
    }
    if (!file_exists(paths.journal)) {
        return fallback_recovery(
            &paths, game_id, slot_id, object_workspace,
            object_workspace_bytes);
    }
    journal_t journal;
    if (!read_journal(paths.journal, game_id, slot_id, &journal)) {
        return fallback_recovery(
            &paths, game_id, slot_id, object_workspace,
            object_workspace_bytes);
    }
    return resume_journal(
        store, &paths, &journal, game_id, slot_id, object_workspace,
        object_workspace_bytes);
}

p4_game_save_store_result_t p4_game_save_store_load(
    p4_game_save_store_t *store,
    const char *game_id,
    const char *slot_id,
    uint8_t *object_workspace,
    size_t object_workspace_bytes,
    uint8_t *payload_output,
    size_t payload_capacity,
    size_t *payload_bytes_out,
    uint32_t *schema_version_out,
    uint32_t *sequence_out)
{
    if (payload_bytes_out != NULL) {
        *payload_bytes_out = 0U;
    }
    if (schema_version_out != NULL) {
        *schema_version_out = 0U;
    }
    if (sequence_out != NULL) {
        *sequence_out = 0U;
    }
    const p4_game_save_store_result_t mode = mode_for_read(store);
    if (mode != P4_GAME_SAVE_STORE_OK || object_workspace == NULL ||
        object_workspace_bytes < P4_GAME_SAVE_MAX_FILE_BYTES ||
        payload_bytes_out == NULL || schema_version_out == NULL ||
        sequence_out == NULL) {
        return mode == P4_GAME_SAVE_STORE_OK
            ? P4_GAME_SAVE_STORE_BAD_ARGUMENT : mode;
    }
    save_paths_t paths;
    if (!compose_paths(store, game_id, slot_id, &paths)) {
        return P4_GAME_SAVE_STORE_BAD_ARGUMENT;
    }
    if (store->mode == P4_GAME_SAVE_STORAGE_WRITABLE) {
        const p4_game_save_store_result_t recovered =
            p4_game_save_store_recover(
                store, game_id, slot_id, object_workspace,
                object_workspace_bytes);
        if (recovered != P4_GAME_SAVE_STORE_OK &&
            recovered != P4_GAME_SAVE_STORE_NOT_FOUND) {
            return recovered;
        }
    }
    const object_file_t current = read_object(
        paths.current, game_id, slot_id, object_workspace,
        object_workspace_bytes);
    if (current.result != P4_GAME_SAVE_STORE_OK) {
        return current.result;
    }
    if (payload_output == NULL || payload_capacity < current.info.payload_bytes) {
        return P4_GAME_SAVE_STORE_BAD_ARGUMENT;
    }
    memcpy(payload_output,
           object_workspace + current.info.payload_offset,
           current.info.payload_bytes);
    *payload_bytes_out = current.info.payload_bytes;
    *schema_version_out = current.info.schema_version;
    *sequence_out = current.info.sequence;
    return P4_GAME_SAVE_STORE_OK;
}

p4_game_save_store_result_t p4_game_save_store_commit(
    p4_game_save_store_t *store,
    const char *game_id,
    const char *slot_id,
    uint32_t schema_version,
    uint32_t expected_sequence,
    const uint8_t *payload,
    size_t payload_bytes,
    uint8_t *object_workspace,
    size_t object_workspace_bytes,
    uint32_t *committed_sequence_out)
{
    if (committed_sequence_out != NULL) {
        *committed_sequence_out = 0U;
    }
    if (store == NULL || !store->initialized ||
        committed_sequence_out == NULL || object_workspace == NULL ||
        object_workspace_bytes < P4_GAME_SAVE_MAX_FILE_BYTES ||
        payload == NULL || payload_bytes == 0U ||
        payload_bytes > P4_GAME_SAVE_MAX_BYTES || schema_version == 0U) {
        return P4_GAME_SAVE_STORE_BAD_ARGUMENT;
    }
    if (store->mode == P4_GAME_SAVE_STORAGE_READ_ONLY) {
        return P4_GAME_SAVE_STORE_READ_ONLY;
    }
    if (store->mode != P4_GAME_SAVE_STORAGE_WRITABLE) {
        return P4_GAME_SAVE_STORE_UNAVAILABLE;
    }
    save_paths_t paths;
    if (!compose_paths(store, game_id, slot_id, &paths) ||
        !ensure_save_directories(store, &paths)) {
        return P4_GAME_SAVE_STORE_IO_ERROR;
    }
    const p4_game_save_store_result_t recovered =
        p4_game_save_store_recover(
            store, game_id, slot_id, object_workspace,
            object_workspace_bytes);
    if (recovered != P4_GAME_SAVE_STORE_OK &&
        recovered != P4_GAME_SAVE_STORE_NOT_FOUND) {
        return recovered;
    }
    const object_file_t current = read_object(
        paths.current, game_id, slot_id, object_workspace,
        object_workspace_bytes);
    const uint32_t current_sequence =
        current.result == P4_GAME_SAVE_STORE_OK ? current.info.sequence : 0U;
    if ((current.result != P4_GAME_SAVE_STORE_OK &&
         current.result != P4_GAME_SAVE_STORE_NOT_FOUND) ||
        current_sequence != expected_sequence || expected_sequence == UINT32_MAX) {
        return current.result == P4_GAME_SAVE_STORE_OK ||
               current.result == P4_GAME_SAVE_STORE_NOT_FOUND
            ? P4_GAME_SAVE_STORE_CONFLICT : current.result;
    }
    const uint32_t target_sequence = expected_sequence + 1U;
    size_t object_bytes = 0U;
    if (p4_game_save_encode(
            game_id, slot_id, schema_version, target_sequence, payload,
            payload_bytes, object_workspace, object_workspace_bytes,
            &object_bytes) != P4_GAME_SAVE_VALID ||
        !write_synced(paths.stage, object_workspace, object_bytes)) {
        return P4_GAME_SAVE_STORE_IO_ERROR;
    }
    object_file_t stage = read_object(
        paths.stage, game_id, slot_id, object_workspace,
        object_workspace_bytes);
    if (stage.result != P4_GAME_SAVE_STORE_OK ||
        stage.info.sequence != target_sequence) {
        return stage.result == P4_GAME_SAVE_STORE_OK
            ? P4_GAME_SAVE_STORE_CORRUPT : stage.result;
    }
    if (!transition_allowed(store, P4_GAME_SAVE_TRANSITION_STAGE_SYNCED)) {
        return P4_GAME_SAVE_STORE_INTERRUPTED;
    }
    journal_t journal = {
        .state = JOURNAL_STAGED,
        .expected_sequence = expected_sequence,
        .target_sequence = target_sequence,
    };
    memcpy(journal.game_id, game_id, strlen(game_id) + 1U);
    memcpy(journal.slot_id, slot_id, strlen(slot_id) + 1U);
    memcpy(journal.object_sha256, stage.info.object_sha256,
           sizeof(journal.object_sha256));
    if (!write_journal(paths.journal, &journal, JOURNAL_STAGED)) {
        return P4_GAME_SAVE_STORE_IO_ERROR;
    }
    if (!transition_allowed(store, P4_GAME_SAVE_TRANSITION_JOURNAL_STAGED)) {
        return P4_GAME_SAVE_STORE_INTERRUPTED;
    }
    const p4_game_save_store_result_t result = resume_journal(
        store, &paths, &journal, game_id, slot_id, object_workspace,
        object_workspace_bytes);
    if (result == P4_GAME_SAVE_STORE_OK) {
        *committed_sequence_out = target_sequence;
    }
    return result;
}

const char *p4_game_save_store_result_name(
    p4_game_save_store_result_t result)
{
    switch (result) {
    case P4_GAME_SAVE_STORE_OK: return "ok";
    case P4_GAME_SAVE_STORE_NOT_FOUND: return "not-found";
    case P4_GAME_SAVE_STORE_CONFLICT: return "conflict";
    case P4_GAME_SAVE_STORE_READ_ONLY: return "read-only";
    case P4_GAME_SAVE_STORE_UNAVAILABLE: return "unavailable";
    case P4_GAME_SAVE_STORE_CORRUPT: return "corrupt";
    case P4_GAME_SAVE_STORE_IO_ERROR: return "io-error";
    case P4_GAME_SAVE_STORE_INTERRUPTED: return "interrupted";
    case P4_GAME_SAVE_STORE_BAD_ARGUMENT: return "bad-argument";
    default: return "unknown";
    }
}
