// SPDX-License-Identifier: MIT

#define _POSIX_C_SOURCE 200809L

#include "p4/game_save.h"
#include "p4/game_save_service.h"
#include "p4/game_save_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static void test_container(void)
{
    static const uint8_t expected_abc_sha256[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    const uint8_t payload[] = {'a', 'b', 'c'};
    uint8_t object[P4_GAME_SAVE_MAX_FILE_BYTES];
    uint8_t duplicate[P4_GAME_SAVE_MAX_FILE_BYTES];
    size_t object_bytes = 0U;
    size_t duplicate_bytes = 0U;
    CHECK(p4_game_save_encode(
        "org.p4console.lord", "AUTO", 1U, 7U, payload,
        sizeof(payload), object, sizeof(object), &object_bytes) ==
          P4_GAME_SAVE_VALID);
    CHECK(p4_game_save_encode(
        "org.p4console.lord", "AUTO", 1U, 7U, payload,
        sizeof(payload), duplicate, sizeof(duplicate), &duplicate_bytes) ==
          P4_GAME_SAVE_VALID);
    CHECK(object_bytes == P4_GAME_SAVE_HEADER_BYTES + sizeof(payload));
    CHECK(object_bytes == duplicate_bytes);
    CHECK(memcmp(object, duplicate, object_bytes) == 0);
    CHECK(memcmp(object + 104U, expected_abc_sha256,
                 sizeof(expected_abc_sha256)) == 0);

    p4_game_save_info_t info;
    CHECK(p4_game_save_parse(
        object, object_bytes, "org.p4console.lord", "AUTO", &info) ==
          P4_GAME_SAVE_VALID);
    CHECK(strcmp(info.game_id, "org.p4console.lord") == 0);
    CHECK(strcmp(info.slot_id, "AUTO") == 0);
    CHECK(info.schema_version == 1U);
    CHECK(info.sequence == 7U);
    CHECK(info.payload_bytes == sizeof(payload));
    CHECK(memcmp(object + info.payload_offset, payload, sizeof(payload)) == 0);

    for (size_t bytes = 0U; bytes < object_bytes; ++bytes) {
        CHECK(p4_game_save_parse(
            object, bytes, "org.p4console.lord", "AUTO", &info) !=
              P4_GAME_SAVE_VALID);
    }
    CHECK(p4_game_save_parse(
        object, object_bytes, "org.p4console.other", "AUTO", &info) ==
          P4_GAME_SAVE_BAD_ID);
    CHECK(p4_game_save_parse(
        object, object_bytes, "org.p4console.lord", "SLOT2", &info) ==
          P4_GAME_SAVE_BAD_ID);

    duplicate[object_bytes - 1U] ^= UINT8_C(1);
    CHECK(p4_game_save_parse(
        duplicate, duplicate_bytes, NULL, NULL, &info) ==
          P4_GAME_SAVE_BAD_DIGEST);
    memcpy(duplicate, object, object_bytes);
    duplicate[200] = UINT8_C(1);
    CHECK(p4_game_save_parse(
        duplicate, duplicate_bytes, NULL, NULL, &info) ==
          P4_GAME_SAVE_BAD_LAYOUT);
    memcpy(duplicate, object, object_bytes);
    duplicate[41] = 0U;
    CHECK(p4_game_save_parse(
        duplicate, duplicate_bytes, NULL, NULL, &info) ==
          P4_GAME_SAVE_BAD_ID);

    CHECK(!p4_game_save_game_id_valid("../lord"));
    CHECK(!p4_game_save_game_id_valid("Org.p4.lord"));
    CHECK(!p4_game_save_slot_id_valid("../AUTO"));
    CHECK(!p4_game_save_slot_id_valid("_AUTO"));
    CHECK(p4_game_save_slot_id_valid("SLOT-2"));
    CHECK(strcmp(p4_game_save_result_name(P4_GAME_SAVE_BAD_DIGEST),
                 "bad-digest") == 0);
}

static void test_maximum_container(void)
{
    uint8_t *const payload = malloc(P4_GAME_SAVE_MAX_BYTES);
    uint8_t *const object = malloc(P4_GAME_SAVE_MAX_FILE_BYTES);
    CHECK(payload != NULL && object != NULL);
    if (payload == NULL || object == NULL) {
        free(payload);
        free(object);
        return;
    }
    memset(payload, 0x5a, P4_GAME_SAVE_MAX_BYTES);
    size_t bytes = 0U;
    CHECK(p4_game_save_encode(
        "org.p4.maximum", "SLOT2", 9U, UINT32_MAX, payload,
        P4_GAME_SAVE_MAX_BYTES, object, P4_GAME_SAVE_MAX_FILE_BYTES,
        &bytes) == P4_GAME_SAVE_VALID);
    CHECK(bytes == P4_GAME_SAVE_MAX_FILE_BYTES);
    p4_game_save_info_t info;
    CHECK(p4_game_save_parse(
        object, bytes, "org.p4.maximum", "SLOT2", &info) ==
          P4_GAME_SAVE_VALID);
    CHECK(p4_game_save_encode(
        "org.p4.maximum", "SLOT2", 9U, 1U, payload,
        P4_GAME_SAVE_MAX_BYTES + 1U, object,
        P4_GAME_SAVE_MAX_FILE_BYTES, &bytes) == P4_GAME_SAVE_BAD_SIZE);
    free(payload);
    free(object);
}

static void test_memory_backend(void)
{
    uint8_t *const workspace = malloc(P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES);
    CHECK(workspace != NULL);
    if (workspace == NULL) {
        return;
    }
    p4_game_save_memory_t memory;
    CHECK(p4_game_save_memory_init(
        &memory, "org.p4console.lord", workspace,
        P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES));
    uint8_t snapshot[16];
    size_t snapshot_bytes = 99U;
    uint32_t schema = 99U;
    uint32_t sequence = 99U;
    CHECK(p4_game_save_memory_copy_snapshot(
        &memory, "AUTO", NULL, 0U, &snapshot_bytes, &schema, &sequence));
    CHECK(snapshot_bytes == 0U && schema == 0U && sequence == 0U);

    const uint8_t initial[] = {1U, 2U, 3U};
    CHECK(p4_game_save_memory_seed(
        &memory, "AUTO", 1U, 4U, initial, sizeof(initial)));
    CHECK(p4_game_save_memory_copy_snapshot(
        &memory, "AUTO", snapshot, sizeof(snapshot), &snapshot_bytes,
        &schema, &sequence));
    CHECK(snapshot_bytes == sizeof(initial));
    CHECK(schema == 1U && sequence == 4U);
    CHECK(memcmp(snapshot, initial, sizeof(initial)) == 0);

    uint8_t next[] = {9U, 8U, 7U, 6U};
    p4_game_save_ticket_t ticket = P4_GAME_SAVE_INVALID_TICKET;
    CHECK(p4_game_save_memory_queue(
        &memory, "AUTO", 2U, 4U, next, sizeof(next), &ticket));
    CHECK(ticket != P4_GAME_SAVE_INVALID_TICKET);
    next[0] = 0U;
    p4_game_save_status_t status = P4_GAME_SAVE_NONE;
    uint32_t committed = 99U;
    CHECK(p4_game_save_memory_read_status(
        &memory, ticket, &status, &committed));
    CHECK(status == P4_GAME_SAVE_QUEUED && committed == 0U);
    const uint8_t duplicate[] = {9U, 8U, 7U, 6U};
    p4_game_save_ticket_t duplicate_ticket = P4_GAME_SAVE_INVALID_TICKET;
    CHECK(p4_game_save_memory_queue(
        &memory, "AUTO", 2U, 4U, duplicate, sizeof(duplicate),
        &duplicate_ticket));
    CHECK(duplicate_ticket == ticket);
    const uint8_t blocked[] = {5U};
    CHECK(!p4_game_save_memory_queue(
        &memory, "AUTO", 2U, 4U, blocked, sizeof(blocked),
        &duplicate_ticket));
    CHECK(p4_game_save_memory_process(&memory, 1U) == 1U);
    CHECK(p4_game_save_memory_read_status(
        &memory, ticket, &status, &committed));
    CHECK(status == P4_GAME_SAVE_COMMITTED && committed == 5U);
    CHECK(p4_game_save_memory_copy_snapshot(
        &memory, "AUTO", snapshot, sizeof(snapshot), &snapshot_bytes,
        &schema, &sequence));
    CHECK(snapshot_bytes == sizeof(duplicate));
    CHECK(snapshot[0] == 9U && schema == 2U && sequence == 5U);

    const uint8_t stale[] = {3U, 3U};
    p4_game_save_ticket_t stale_ticket = P4_GAME_SAVE_INVALID_TICKET;
    CHECK(p4_game_save_memory_queue(
        &memory, "AUTO", 2U, 4U, stale, sizeof(stale), &stale_ticket));
    CHECK(p4_game_save_memory_process(&memory, 1U) == 1U);
    CHECK(p4_game_save_memory_read_status(
        &memory, stale_ticket, &status, &committed));
    CHECK(status == P4_GAME_SAVE_CONFLICT && committed == 5U);
    CHECK(!p4_game_save_memory_read_status(
        &memory, ticket, &status, &committed));

    const uint8_t second[] = {4U};
    p4_game_save_ticket_t second_ticket = P4_GAME_SAVE_INVALID_TICKET;
    CHECK(p4_game_save_memory_queue(
        &memory, "SLOT2", 1U, 0U, second, sizeof(second), &second_ticket));
    CHECK(p4_game_save_memory_process(&memory, 2U) == 1U);
    CHECK(p4_game_save_memory_read_status(
        &memory, second_ticket, &status, &committed));
    CHECK(status == P4_GAME_SAVE_COMMITTED && committed == 1U);
    CHECK(!p4_game_save_memory_queue(
        &memory, "SLOT3", 1U, 0U, second, sizeof(second), &second_ticket));
    free(workspace);
}

typedef struct {
    p4_game_save_transition_t fail_at;
} fault_hook_t;

static bool stop_at_transition(void *context,
                               p4_game_save_transition_t transition)
{
    const fault_hook_t *const fault = context;
    return fault == NULL || transition != fault->fail_at;
}

static bool make_temp_root(char root[64])
{
    memcpy(root, "/tmp/p4-game-save-XXXXXX",
           sizeof("/tmp/p4-game-save-XXXXXX"));
    const int descriptor = mkstemp(root);
    if (descriptor < 0) {
        return false;
    }
    const bool closed = close(descriptor) == 0;
    const bool removed = unlink(root) == 0;
    return closed && removed && mkdir(root, 0700) == 0;
}

static void cleanup_temp_root(const char *root)
{
    char path[384];
    static const char *const files[] = {
        "AUTO.P4SAVE",
        "AUTO.P4SAVE.STAGE",
        "AUTO.P4SAVE.BAK",
        "AUTO.P4SAVE.JOURNAL",
    };
    for (size_t index = 0U; index < sizeof(files) / sizeof(files[0]);
         ++index) {
        (void)snprintf(path, sizeof(path),
                       "%s/SAVES/org.p4console.lord/%s",
                       root, files[index]);
        (void)unlink(path);
    }
    (void)snprintf(path, sizeof(path),
                   "%s/SAVES/org.p4console.lord", root);
    (void)rmdir(path);
    (void)snprintf(path, sizeof(path), "%s/SAVES", root);
    (void)rmdir(path);
    (void)rmdir(root);
}

static void test_store_round_trip(void)
{
    char root[64] = {0};
    CHECK(make_temp_root(root));
    if (root[0] == '\0') {
        return;
    }
    uint8_t *const workspace = malloc(P4_GAME_SAVE_MAX_FILE_BYTES);
    CHECK(workspace != NULL);
    if (workspace == NULL) {
        cleanup_temp_root(root);
        return;
    }
    p4_game_save_store_t store;
    CHECK(p4_game_save_store_init(
        &store, root, P4_GAME_SAVE_STORAGE_WRITABLE));
    const uint8_t first[] = {1U, 2U, 3U};
    uint32_t committed = 99U;
    CHECK(p4_game_save_store_commit(
        &store, "org.p4console.lord", "AUTO", 1U, 0U,
        first, sizeof(first), workspace, P4_GAME_SAVE_MAX_FILE_BYTES,
        &committed) == P4_GAME_SAVE_STORE_OK);
    CHECK(committed == 1U);

    uint8_t loaded[16];
    size_t loaded_bytes = 0U;
    uint32_t schema = 0U;
    uint32_t sequence = 0U;
    CHECK(p4_game_save_store_load(
        &store, "org.p4console.lord", "AUTO", workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, loaded, sizeof(loaded), &loaded_bytes,
        &schema, &sequence) == P4_GAME_SAVE_STORE_OK);
    CHECK(loaded_bytes == sizeof(first));
    CHECK(schema == 1U && sequence == 1U);
    CHECK(memcmp(loaded, first, sizeof(first)) == 0);

    CHECK(p4_game_save_store_commit(
        &store, "org.p4console.lord", "AUTO", 1U, 0U,
        first, sizeof(first), workspace, P4_GAME_SAVE_MAX_FILE_BYTES,
        &committed) == P4_GAME_SAVE_STORE_CONFLICT);
    p4_game_save_store_set_mode(&store, P4_GAME_SAVE_STORAGE_READ_ONLY);
    CHECK(p4_game_save_store_commit(
        &store, "org.p4console.lord", "AUTO", 1U, 1U,
        first, sizeof(first), workspace, P4_GAME_SAVE_MAX_FILE_BYTES,
        &committed) == P4_GAME_SAVE_STORE_READ_ONLY);
    CHECK(p4_game_save_store_load(
        &store, "org.p4console.lord", "AUTO", workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, loaded, sizeof(loaded), &loaded_bytes,
        &schema, &sequence) == P4_GAME_SAVE_STORE_OK);
    p4_game_save_store_set_mode(&store, P4_GAME_SAVE_STORAGE_HOST_OWNED);
    CHECK(p4_game_save_store_load(
        &store, "org.p4console.lord", "AUTO", workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, loaded, sizeof(loaded), &loaded_bytes,
        &schema, &sequence) == P4_GAME_SAVE_STORE_UNAVAILABLE);
    p4_game_save_store_set_mode(&store, P4_GAME_SAVE_STORAGE_WRITABLE);

    const uint8_t second[] = {4U, 5U, 6U, 7U};
    CHECK(p4_game_save_store_commit(
        &store, "org.p4console.lord", "AUTO", 2U, 1U,
        second, sizeof(second), workspace, P4_GAME_SAVE_MAX_FILE_BYTES,
        &committed) == P4_GAME_SAVE_STORE_OK);
    CHECK(committed == 2U);

    char current[384];
    (void)snprintf(current, sizeof(current),
                   "%s/SAVES/org.p4console.lord/AUTO.P4SAVE", root);
    FILE *const file = fopen(current, "r+b");
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(fseek(file, -1L, SEEK_END) == 0);
        const int value = fgetc(file);
        CHECK(value != EOF);
        CHECK(fseek(file, -1L, SEEK_END) == 0);
        CHECK(fputc(value ^ 1, file) != EOF);
        CHECK(fflush(file) == 0);
        CHECK(fclose(file) == 0);
    }
    CHECK(p4_game_save_store_recover(
        &store, "org.p4console.lord", "AUTO", workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES) == P4_GAME_SAVE_STORE_OK);
    CHECK(p4_game_save_store_load(
        &store, "org.p4console.lord", "AUTO", workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, loaded, sizeof(loaded), &loaded_bytes,
        &schema, &sequence) == P4_GAME_SAVE_STORE_OK);
    CHECK(sequence == 1U);
    CHECK(memcmp(loaded, first, sizeof(first)) == 0);
    CHECK(strcmp(p4_game_save_store_result_name(
                     P4_GAME_SAVE_STORE_CONFLICT), "conflict") == 0);
    free(workspace);
    cleanup_temp_root(root);
}

static void test_store_power_loss(void)
{
    for (p4_game_save_transition_t transition =
             P4_GAME_SAVE_TRANSITION_STAGE_SYNCED;
         transition <= P4_GAME_SAVE_TRANSITION_JOURNAL_REMOVED;
         transition = (p4_game_save_transition_t)((int)transition + 1)) {
        char root[64] = {0};
        CHECK(make_temp_root(root));
        uint8_t *const workspace = malloc(P4_GAME_SAVE_MAX_FILE_BYTES);
        CHECK(workspace != NULL);
        if (root[0] == '\0' || workspace == NULL) {
            free(workspace);
            if (root[0] != '\0') {
                cleanup_temp_root(root);
            }
            continue;
        }
        p4_game_save_store_t store;
        CHECK(p4_game_save_store_init(
            &store, root, P4_GAME_SAVE_STORAGE_WRITABLE));
        const uint8_t first[] = {1U};
        const uint8_t second[] = {2U};
        uint32_t committed = 0U;
        CHECK(p4_game_save_store_commit(
            &store, "org.p4console.lord", "AUTO", 1U, 0U,
            first, sizeof(first), workspace, P4_GAME_SAVE_MAX_FILE_BYTES,
            &committed) == P4_GAME_SAVE_STORE_OK);
        fault_hook_t fault = {.fail_at = transition};
        p4_game_save_store_set_transition_hook(
            &store, stop_at_transition, &fault);
        CHECK(p4_game_save_store_commit(
            &store, "org.p4console.lord", "AUTO", 1U, 1U,
            second, sizeof(second), workspace, P4_GAME_SAVE_MAX_FILE_BYTES,
            &committed) == P4_GAME_SAVE_STORE_INTERRUPTED);
        p4_game_save_store_set_transition_hook(&store, NULL, NULL);
        CHECK(p4_game_save_store_recover(
            &store, "org.p4console.lord", "AUTO", workspace,
            P4_GAME_SAVE_MAX_FILE_BYTES) == P4_GAME_SAVE_STORE_OK);
        uint8_t loaded[4] = {0};
        size_t loaded_bytes = 0U;
        uint32_t schema = 0U;
        uint32_t sequence = 0U;
        CHECK(p4_game_save_store_load(
            &store, "org.p4console.lord", "AUTO", workspace,
            P4_GAME_SAVE_MAX_FILE_BYTES, loaded, sizeof(loaded),
            &loaded_bytes, &schema, &sequence) == P4_GAME_SAVE_STORE_OK);
        const uint32_t expected_sequence =
            transition == P4_GAME_SAVE_TRANSITION_STAGE_SYNCED ? 1U : 2U;
        CHECK(sequence == expected_sequence);
        CHECK(loaded_bytes == 1U);
        CHECK(loaded[0] == (expected_sequence == 1U ? 1U : 2U));
        free(workspace);
        cleanup_temp_root(root);
    }
}

static void test_durable_service(void)
{
    char root[64] = {0};
    CHECK(make_temp_root(root));
    uint8_t *const queue_workspace =
        malloc(P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES);
    uint8_t *const object_workspace =
        malloc(P4_GAME_SAVE_MAX_FILE_BYTES);
    uint8_t *const launch_snapshot = malloc(P4_GAME_SAVE_MAX_BYTES);
    CHECK(queue_workspace != NULL && object_workspace != NULL &&
          launch_snapshot != NULL);
    if (root[0] == '\0' || queue_workspace == NULL ||
        object_workspace == NULL || launch_snapshot == NULL) {
        free(queue_workspace);
        free(object_workspace);
        free(launch_snapshot);
        if (root[0] != '\0') {
            cleanup_temp_root(root);
        }
        return;
    }
    p4_game_save_service_t service;
    CHECK(p4_game_save_service_init(
        &service, root, P4_GAME_SAVE_STORAGE_WRITABLE,
        "org.p4console.lord", "AUTO", queue_workspace,
        P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES, object_workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, launch_snapshot,
        P4_GAME_SAVE_MAX_BYTES));
    CHECK(p4_game_save_service_available(&service));
    CHECK(service.launch_snapshot_bytes == 0U);
    uint8_t payload[] = {7U, 8U, 9U};
    p4_game_save_ticket_t ticket = P4_GAME_SAVE_INVALID_TICKET;
    CHECK(p4_game_save_service_queue(
        &service, "AUTO", 1U, 0U, payload, sizeof(payload), &ticket));
    payload[0] = 0U;
    CHECK(p4_game_save_service_process(&service, 1U) == 1U);
    p4_game_save_status_t status = P4_GAME_SAVE_NONE;
    uint32_t sequence = 0U;
    CHECK(p4_game_save_service_read_status(
        &service, ticket, &status, &sequence));
    CHECK(status == P4_GAME_SAVE_COMMITTED && sequence == 1U);

    uint8_t *const queue_workspace2 =
        malloc(P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES);
    uint8_t *const object_workspace2 =
        malloc(P4_GAME_SAVE_MAX_FILE_BYTES);
    uint8_t *const launch_snapshot2 = malloc(P4_GAME_SAVE_MAX_BYTES);
    CHECK(queue_workspace2 != NULL && object_workspace2 != NULL &&
          launch_snapshot2 != NULL);
    if (queue_workspace2 != NULL && object_workspace2 != NULL &&
        launch_snapshot2 != NULL) {
        p4_game_save_service_t reloaded;
        CHECK(p4_game_save_service_init(
            &reloaded, root, P4_GAME_SAVE_STORAGE_WRITABLE,
            "org.p4console.lord", "AUTO", queue_workspace2,
            P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES, object_workspace2,
            P4_GAME_SAVE_MAX_FILE_BYTES, launch_snapshot2,
            P4_GAME_SAVE_MAX_BYTES));
        CHECK(p4_game_save_service_available(&reloaded));
        CHECK(reloaded.launch_snapshot_bytes == 3U);
        CHECK(reloaded.launch_schema_version == 1U);
        CHECK(reloaded.launch_sequence == 1U);
        CHECK(launch_snapshot2[0] == 7U);

        const uint8_t stale[] = {4U};
        p4_game_save_ticket_t stale_ticket =
            P4_GAME_SAVE_INVALID_TICKET;
        CHECK(p4_game_save_service_queue(
            &reloaded, "AUTO", 1U, 0U, stale, sizeof(stale),
            &stale_ticket));
        CHECK(p4_game_save_service_process(&reloaded, 1U) == 1U);
        CHECK(p4_game_save_service_read_status(
            &reloaded, stale_ticket, &status, &sequence));
        CHECK(status == P4_GAME_SAVE_CONFLICT && sequence == 1U);
        p4_game_save_service_set_storage_mode(
            &reloaded, P4_GAME_SAVE_STORAGE_HOST_OWNED);
        CHECK(!p4_game_save_service_available(&reloaded));
        CHECK(!p4_game_save_service_queue(
            &reloaded, "AUTO", 1U, 1U, stale, sizeof(stale),
            &stale_ticket));
    }
    free(queue_workspace2);
    free(object_workspace2);
    free(launch_snapshot2);
    free(queue_workspace);
    free(object_workspace);
    free(launch_snapshot);
    cleanup_temp_root(root);
}

int main(void)
{
    test_container();
    test_maximum_container();
    test_memory_backend();
    test_store_round_trip();
    test_store_power_loss();
    test_durable_service();
    if (s_failures != 0) {
        fprintf(stderr, "%d save test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("p4 game save tests passed");
    return EXIT_SUCCESS;
}
