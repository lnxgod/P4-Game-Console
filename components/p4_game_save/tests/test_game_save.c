// SPDX-License-Identifier: MIT

#define _POSIX_C_SOURCE 200809L

#include "p4/game_save.h"
#include "p4/game_save_service.h"
#include "p4/game_save_store.h"
#include "../src/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int s_failures;

static const p4_game_save_protection_t s_protection = {
    .key = {
        0x10U, 0x21U, 0x32U, 0x43U, 0x54U, 0x65U, 0x76U, 0x87U,
        0x98U, 0xa9U, 0xbaU, 0xcbU, 0xdcU, 0xedU, 0xfeU, 0x0fU,
        0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U,
        0x99U, 0xaaU, 0xbbU, 0xccU, 0xddU, 0xeeU, 0xffU, 0x01U,
    },
};

static const p4_game_save_protection_t s_other_protection = {
    .key = {
        0x90U, 0x81U, 0x72U, 0x63U, 0x54U, 0x45U, 0x36U, 0x27U,
        0x18U, 0x09U, 0xfaU, 0xebU, 0xdcU, 0xcdU, 0xbeU, 0xafU,
        0x91U, 0x82U, 0x73U, 0x64U, 0x55U, 0x46U, 0x37U, 0x28U,
        0x19U, 0x0aU, 0xfbU, 0xecU, 0xddU, 0xceU, 0xbfU, 0xa0U,
    },
};

typedef struct {
    bool closed;
    bool fail_query;
    bool fail_close;
    bool fail_object_query;
    bool fail_object_advance;
    bool fail_object_sequence;
    bool anchor_present;
    uint32_t anchor_sequence;
    uint8_t anchor_sha256[P4_GAME_SAVE_SHA256_BYTES];
    unsigned query_count;
    unsigned close_count;
    unsigned object_query_count;
    unsigned object_advance_count;
} test_legacy_marker_t;

static bool test_legacy_query(
    void *context, const char *game_id, const char *slot_id,
    bool *allowed_out)
{
    test_legacy_marker_t *const marker = context;
    if (marker == NULL || allowed_out == NULL ||
        !p4_game_save_game_id_valid(game_id) ||
        !p4_game_save_slot_id_valid(slot_id) || marker->fail_query) {
        return false;
    }
    ++marker->query_count;
    *allowed_out = !marker->closed;
    return true;
}

static bool test_legacy_close(
    void *context, const char *game_id, const char *slot_id)
{
    test_legacy_marker_t *const marker = context;
    if (marker == NULL || !p4_game_save_game_id_valid(game_id) ||
        !p4_game_save_slot_id_valid(slot_id) || marker->fail_close) {
        return false;
    }
    ++marker->close_count;
    marker->closed = true;
    return true;
}

static bool test_object_query(
    void *context, const char *game_id, const char *slot_id,
    uint32_t sequence,
    const uint8_t object_sha256[P4_GAME_SAVE_SHA256_BYTES],
    bool *allowed_out)
{
    test_legacy_marker_t *const marker = context;
    if (marker == NULL || allowed_out == NULL || sequence == 0U ||
        object_sha256 == NULL ||
        !p4_game_save_game_id_valid(game_id) ||
        !p4_game_save_slot_id_valid(slot_id) ||
        marker->fail_object_query) {
        return false;
    }
    ++marker->object_query_count;
    *allowed_out = !marker->anchor_present ||
        sequence > marker->anchor_sequence ||
        (sequence == marker->anchor_sequence &&
         memcmp(object_sha256, marker->anchor_sha256,
                sizeof(marker->anchor_sha256)) == 0);
    return true;
}

static bool test_object_advance(
    void *context, const char *game_id, const char *slot_id,
    uint32_t sequence,
    const uint8_t object_sha256[P4_GAME_SAVE_SHA256_BYTES])
{
    test_legacy_marker_t *const marker = context;
    if (marker == NULL || sequence == 0U || object_sha256 == NULL ||
        !p4_game_save_game_id_valid(game_id) ||
        !p4_game_save_slot_id_valid(slot_id) ||
        marker->fail_object_advance ||
        (marker->anchor_present &&
         (sequence < marker->anchor_sequence ||
          (sequence == marker->anchor_sequence &&
           memcmp(object_sha256, marker->anchor_sha256,
                  sizeof(marker->anchor_sha256)) != 0)))) {
        return false;
    }
    ++marker->object_advance_count;
    marker->closed = true;
    marker->anchor_present = true;
    marker->anchor_sequence = sequence;
    memcpy(marker->anchor_sha256, object_sha256,
           sizeof(marker->anchor_sha256));
    return true;
}

static bool test_object_sequence(
    void *context, const char *game_id, const char *slot_id,
    uint32_t *sequence_out)
{
    test_legacy_marker_t *const marker = context;
    if (marker == NULL || sequence_out == NULL ||
        !p4_game_save_game_id_valid(game_id) ||
        !p4_game_save_slot_id_valid(slot_id) ||
        marker->fail_object_sequence) {
        return false;
    }
    *sequence_out = marker->anchor_present
        ? marker->anchor_sequence : 0U;
    return true;
}

static p4_game_save_legacy_policy_t test_legacy_policy(
    test_legacy_marker_t *marker)
{
    const p4_game_save_legacy_policy_t policy = {
        .query = test_legacy_query,
        .close = test_legacy_close,
        .object_query = test_object_query,
        .object_advance = test_object_advance,
        .object_sequence = test_object_sequence,
        .context = marker,
    };
    return policy;
}

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static void test_hash(const uint8_t *data, size_t bytes,
                      uint8_t digest[P4_GAME_SAVE_SHA256_BYTES])
{
    p4_game_save_sha256_t hash;
    p4_game_save_sha256_init(&hash);
    p4_game_save_sha256_update(&hash, data, bytes);
    p4_game_save_sha256_finish(&hash, digest);
}

static void recompute_ordinary_digests(uint8_t *object, size_t object_bytes)
{
    enum {
        PAYLOAD_SHA256 = 104,
        OBJECT_SHA256 = 136,
    };
    test_hash(object + P4_GAME_SAVE_HEADER_BYTES,
              object_bytes - P4_GAME_SAVE_HEADER_BYTES,
              object + PAYLOAD_SHA256);
    memset(object + OBJECT_SHA256, 0, P4_GAME_SAVE_SHA256_BYTES);
    test_hash(object, object_bytes, object + OBJECT_SHA256);
}

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

static void test_authenticated_container(void)
{
    static const uint8_t expected_hmac_sha256[32] = {
        0x06U, 0xf6U, 0xb1U, 0xa3U, 0x5fU, 0xb7U, 0xfeU, 0xf7U,
        0xeaU, 0x65U, 0x0cU, 0x51U, 0x7dU, 0x60U, 0x70U, 0x08U,
        0xc2U, 0x6dU, 0xc4U, 0xf2U, 0x33U, 0xc1U, 0xd1U, 0xabU,
        0x42U, 0x3dU, 0x78U, 0x4aU, 0x21U, 0x9eU, 0x01U, 0xfaU,
    };
    const uint8_t payload[] = {'s', 'e', 'a', 'l', 'e', 'd'};
    uint8_t object[P4_GAME_SAVE_MAX_FILE_BYTES];
    uint8_t tampered[P4_GAME_SAVE_MAX_FILE_BYTES];
    size_t object_bytes = 0U;
    CHECK(p4_game_save_protection_valid(&s_protection));
    CHECK(p4_game_save_encode_authenticated(
        &s_protection, "org.p4console.lord", "AUTO", 3U, 41U,
        payload, sizeof(payload), object, sizeof(object), &object_bytes) ==
          P4_GAME_SAVE_VALID);
    CHECK(memcmp(object, P4_GAME_SAVE_AUTH_MAGIC, 8U) == 0);
    CHECK(memcmp(object + 192U, expected_hmac_sha256,
                 sizeof(expected_hmac_sha256)) == 0);

    p4_game_save_info_t info;
    CHECK(p4_game_save_parse_authenticated(
        &s_protection, object, object_bytes, "org.p4console.lord", "AUTO",
        &info) == P4_GAME_SAVE_VALID);
    CHECK(info.authenticated);
    CHECK(info.format_version == P4_GAME_SAVE_AUTH_FORMAT_VERSION);
    CHECK(info.schema_version == 3U && info.sequence == 41U);
    CHECK(p4_game_save_parse_authenticated(
        &s_other_protection, object, object_bytes, "org.p4console.lord",
        "AUTO", &info) == P4_GAME_SAVE_BAD_AUTH);
    CHECK(p4_game_save_parse(
        object, object_bytes, NULL, NULL, &info) ==
          P4_GAME_SAVE_BAD_MAGIC);

    /* Recomputing both public SHA-256 fields cannot forge the HMAC. */
    memcpy(tampered, object, object_bytes);
    tampered[P4_GAME_SAVE_HEADER_BYTES] ^= UINT8_C(0x40);
    recompute_ordinary_digests(tampered, object_bytes);
    CHECK(p4_game_save_parse_authenticated(
        &s_protection, tampered, object_bytes, NULL, NULL, &info) ==
          P4_GAME_SAVE_BAD_AUTH);

    /* Schema and sequence are covered even when the ordinary SHA is fixed. */
    memcpy(tampered, object, object_bytes);
    tampered[28U] ^= UINT8_C(1);
    tampered[32U] ^= UINT8_C(1);
    recompute_ordinary_digests(tampered, object_bytes);
    CHECK(p4_game_save_parse_authenticated(
        &s_protection, tampered, object_bytes, NULL, NULL, &info) ==
          P4_GAME_SAVE_BAD_AUTH);

    /* Game and slot fields are covered independently of expected-ID checks. */
    memcpy(tampered, object, object_bytes);
    tampered[40U + 14U] = 'x';
    tampered[88U] = 'B';
    recompute_ordinary_digests(tampered, object_bytes);
    CHECK(p4_game_save_parse_authenticated(
        &s_protection, tampered, object_bytes, NULL, NULL, &info) ==
          P4_GAME_SAVE_BAD_AUTH);

    p4_game_save_protection_t cleared = s_protection;
    p4_game_save_protection_clear(&cleared);
    CHECK(!p4_game_save_protection_valid(&cleared));
    CHECK(p4_game_save_parse_authenticated(
        &cleared, object, object_bytes, NULL, NULL, &info) ==
          P4_GAME_SAVE_BAD_ARGUMENT);
    CHECK(strcmp(p4_game_save_result_name(P4_GAME_SAVE_BAD_AUTH),
                 "bad-auth") == 0);
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
    test_legacy_marker_t marker = {0};
    const p4_game_save_legacy_policy_t legacy_policy =
        test_legacy_policy(&marker);
    CHECK(p4_game_save_store_init(
        &store, root, P4_GAME_SAVE_STORAGE_WRITABLE, &s_protection,
        &legacy_policy));
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
    bool authenticated = false;
    CHECK(p4_game_save_store_load(
        &store, "org.p4console.lord", "AUTO", workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, loaded, sizeof(loaded), &loaded_bytes,
        &schema, &sequence, &authenticated) == P4_GAME_SAVE_STORE_OK);
    CHECK(loaded_bytes == sizeof(first));
    CHECK(schema == 1U && sequence == 1U);
    CHECK(authenticated);
    CHECK(marker.closed);
    CHECK(memcmp(loaded, first, sizeof(first)) == 0);

    p4_game_save_store_t other_device_store;
    CHECK(p4_game_save_store_init(
        &other_device_store, root, P4_GAME_SAVE_STORAGE_WRITABLE,
        &s_other_protection, &legacy_policy));
    CHECK(p4_game_save_store_load(
        &other_device_store, "org.p4console.lord", "AUTO", workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, loaded, sizeof(loaded), &loaded_bytes,
        &schema, &sequence, &authenticated) == P4_GAME_SAVE_STORE_CORRUPT);
    p4_game_save_store_clear(&other_device_store);

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
        &schema, &sequence, &authenticated) == P4_GAME_SAVE_STORE_OK);
    p4_game_save_store_set_mode(&store, P4_GAME_SAVE_STORAGE_HOST_OWNED);
    CHECK(p4_game_save_store_load(
        &store, "org.p4console.lord", "AUTO", workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, loaded, sizeof(loaded), &loaded_bytes,
        &schema, &sequence, &authenticated) ==
          P4_GAME_SAVE_STORE_UNAVAILABLE);
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
    /* Deleting the anchored current must not make its older authenticated
     * backup eligible for promotion. */
    CHECK(unlink(current) == 0);
    CHECK(p4_game_save_store_recover(
        &store, "org.p4console.lord", "AUTO", workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES) == P4_GAME_SAVE_STORE_CORRUPT);
    CHECK(p4_game_save_store_load(
        &store, "org.p4console.lord", "AUTO", workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, loaded, sizeof(loaded), &loaded_bytes,
        &schema, &sequence, &authenticated) == P4_GAME_SAVE_STORE_CORRUPT);
    CHECK(strcmp(p4_game_save_store_result_name(
                     P4_GAME_SAVE_STORE_CONFLICT), "conflict") == 0);
    free(workspace);
    cleanup_temp_root(root);
}

static void test_authenticated_baseline_is_anchored(void)
{
    char root[64] = {0};
    CHECK(make_temp_root(root));
    uint8_t *const workspace = malloc(P4_GAME_SAVE_MAX_FILE_BYTES);
    CHECK(workspace != NULL);
    if (root[0] == '\0' || workspace == NULL) {
        free(workspace);
        if (root[0] != '\0') {
            cleanup_temp_root(root);
        }
        return;
    }
    char saves[384];
    char game_directory[384];
    char current[384];
    (void)snprintf(saves, sizeof(saves), "%s/SAVES", root);
    (void)snprintf(game_directory, sizeof(game_directory),
                   "%s/SAVES/org.p4console.lord", root);
    (void)snprintf(current, sizeof(current),
                   "%s/AUTO.P4SAVE", game_directory);
    CHECK(mkdir(saves, 0700) == 0);
    CHECK(mkdir(game_directory, 0700) == 0);

    const uint8_t payload[] = {0x41U, 0x42U};
    size_t object_bytes = 0U;
    CHECK(p4_game_save_encode_authenticated(
        &s_protection, "org.p4console.lord", "AUTO", 3U, 7U,
        payload, sizeof(payload), workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, &object_bytes) == P4_GAME_SAVE_VALID);
    FILE *file = fopen(current, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(fwrite(workspace, 1U, object_bytes, file) == object_bytes);
        CHECK(fclose(file) == 0);
    }

    test_legacy_marker_t marker = {0};
    const p4_game_save_legacy_policy_t legacy_policy =
        test_legacy_policy(&marker);
    p4_game_save_store_t store;
    CHECK(p4_game_save_store_init(
        &store, root, P4_GAME_SAVE_STORAGE_WRITABLE, &s_protection,
        &legacy_policy));
    uint8_t loaded[4] = {0};
    size_t loaded_bytes = 0U;
    uint32_t schema = 0U;
    uint32_t sequence = 0U;
    bool authenticated = false;
    CHECK(p4_game_save_store_load(
        &store, "org.p4console.lord", "AUTO", workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, loaded, sizeof(loaded),
        &loaded_bytes, &schema, &sequence, &authenticated) ==
          P4_GAME_SAVE_STORE_OK);
    CHECK(authenticated && schema == 3U && sequence == 7U);
    CHECK(loaded_bytes == sizeof(payload) &&
          memcmp(loaded, payload, sizeof(payload)) == 0);
    CHECK(marker.anchor_present && marker.anchor_sequence == 7U);
    CHECK(marker.closed);
    p4_game_save_store_clear(&store);
    free(workspace);
    cleanup_temp_root(root);
}

static void test_same_sequence_alternate_branch_is_rejected(void)
{
    char root[64] = {0};
    CHECK(make_temp_root(root));
    uint8_t *const workspace = malloc(P4_GAME_SAVE_MAX_FILE_BYTES);
    CHECK(workspace != NULL);
    if (root[0] == '\0' || workspace == NULL) {
        free(workspace);
        if (root[0] != '\0') {
            cleanup_temp_root(root);
        }
        return;
    }
    test_legacy_marker_t marker = {0};
    const p4_game_save_legacy_policy_t legacy_policy =
        test_legacy_policy(&marker);
    p4_game_save_store_t store;
    CHECK(p4_game_save_store_init(
        &store, root, P4_GAME_SAVE_STORAGE_WRITABLE, &s_protection,
        &legacy_policy));
    const uint8_t base[] = {1U};
    const uint8_t branch_a[] = {2U};
    const uint8_t branch_b[] = {9U};
    uint32_t committed = 0U;
    CHECK(p4_game_save_store_commit(
        &store, "org.p4console.lord", "AUTO", 1U, 0U,
        base, sizeof(base), workspace, P4_GAME_SAVE_MAX_FILE_BYTES,
        &committed) == P4_GAME_SAVE_STORE_OK);
    CHECK(p4_game_save_store_commit(
        &store, "org.p4console.lord", "AUTO", 1U, 1U,
        branch_a, sizeof(branch_a), workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, &committed) ==
          P4_GAME_SAVE_STORE_OK);
    CHECK(marker.anchor_present && marker.anchor_sequence == 2U);

    size_t alternate_bytes = 0U;
    CHECK(p4_game_save_encode_authenticated(
        &s_protection, "org.p4console.lord", "AUTO", 1U, 2U,
        branch_b, sizeof(branch_b), workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, &alternate_bytes) ==
          P4_GAME_SAVE_VALID);
    char current[384];
    (void)snprintf(current, sizeof(current),
                   "%s/SAVES/org.p4console.lord/AUTO.P4SAVE", root);
    FILE *file = fopen(current, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(fwrite(workspace, 1U, alternate_bytes, file) ==
              alternate_bytes);
        CHECK(fclose(file) == 0);
    }
    CHECK(p4_game_save_store_recover(
        &store, "org.p4console.lord", "AUTO", workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES) == P4_GAME_SAVE_STORE_CORRUPT);
    p4_game_save_store_clear(&store);
    free(workspace);
    cleanup_temp_root(root);
}

static void test_missing_files_resume_from_anchor_floor(void)
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
    test_legacy_marker_t marker = {0};
    const p4_game_save_legacy_policy_t legacy_policy =
        test_legacy_policy(&marker);
    p4_game_save_store_t store;
    CHECK(p4_game_save_store_init(
        &store, root, P4_GAME_SAVE_STORAGE_WRITABLE, &s_protection,
        &legacy_policy));
    const uint8_t first[] = {1U};
    const uint8_t second[] = {2U};
    uint32_t committed = 0U;
    CHECK(p4_game_save_store_commit(
        &store, "org.p4console.lord", "AUTO", 1U, 0U,
        first, sizeof(first), object_workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, &committed) ==
          P4_GAME_SAVE_STORE_OK);
    CHECK(p4_game_save_store_commit(
        &store, "org.p4console.lord", "AUTO", 1U, 1U,
        second, sizeof(second), object_workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, &committed) ==
          P4_GAME_SAVE_STORE_OK);
    CHECK(marker.anchor_present && marker.anchor_sequence == 2U);
    p4_game_save_store_clear(&store);

    char path[384];
    static const char *const files[] = {
        "AUTO.P4SAVE", "AUTO.P4SAVE.BAK", "AUTO.P4SAVE.STAGE",
        "AUTO.P4SAVE.JOURNAL",
    };
    for (size_t index = 0U;
         index < sizeof(files) / sizeof(files[0]); ++index) {
        (void)snprintf(path, sizeof(path),
                       "%s/SAVES/org.p4console.lord/%s",
                       root, files[index]);
        CHECK(unlink(path) == 0 || access(path, F_OK) != 0);
    }

    p4_game_save_service_t service;
    (void)snprintf(path, sizeof(path),
                   "%s/SAVES/org.p4console.lord/AUTO.P4SAVE.STAGE", root);
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(fputc(0x5a, file) != EOF);
        CHECK(fclose(file) == 0);
    }
    CHECK(p4_game_save_service_init(
        &service, root, P4_GAME_SAVE_STORAGE_WRITABLE, &s_protection,
        &legacy_policy, "org.p4console.lord", "AUTO", queue_workspace,
        P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES, object_workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, launch_snapshot,
        P4_GAME_SAVE_MAX_BYTES));
    CHECK(!p4_game_save_service_available(&service));
    CHECK(service.startup_result == P4_GAME_SAVE_STORE_CORRUPT);
    p4_game_save_service_clear(&service);
    CHECK(unlink(path) == 0);
    memset(&service, 0, sizeof(service));
    CHECK(p4_game_save_service_init(
        &service, root, P4_GAME_SAVE_STORAGE_WRITABLE, &s_protection,
        &legacy_policy, "org.p4console.lord", "AUTO", queue_workspace,
        P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES, object_workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, launch_snapshot,
        P4_GAME_SAVE_MAX_BYTES));
    CHECK(p4_game_save_service_available(&service));
    CHECK(service.launch_missing_at_floor);
    CHECK(service.launch_snapshot_bytes == 0U);
    CHECK(service.launch_schema_version == 0U);
    CHECK(service.launch_sequence == 2U);

    /* Compatibility path: a game that sees no payload may still submit
     * expected=0. The service translates that one first request to floor N. */
    const uint8_t reconstructed[] = {3U, 4U};
    p4_game_save_ticket_t ticket = P4_GAME_SAVE_INVALID_TICKET;
    CHECK(p4_game_save_service_queue(
        &service, "AUTO", 2U, 0U, reconstructed,
        sizeof(reconstructed), &ticket));
    CHECK(p4_game_save_service_process(&service, 1U) == 1U);
    p4_game_save_status_t status = P4_GAME_SAVE_NONE;
    uint32_t reported_sequence = 0U;
    CHECK(p4_game_save_service_read_status(
        &service, ticket, &status, &reported_sequence));
    CHECK(status == P4_GAME_SAVE_COMMITTED);
    CHECK(reported_sequence == 3U);
    CHECK(!service.launch_missing_at_floor);
    p4_game_save_service_clear(&service);

    memset(&service, 0, sizeof(service));
    CHECK(p4_game_save_service_init(
        &service, root, P4_GAME_SAVE_STORAGE_WRITABLE, &s_protection,
        &legacy_policy, "org.p4console.lord", "AUTO", queue_workspace,
        P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES, object_workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, launch_snapshot,
        P4_GAME_SAVE_MAX_BYTES));
    CHECK(p4_game_save_service_available(&service));
    CHECK(!service.launch_missing_at_floor);
    CHECK(service.launch_snapshot_bytes == sizeof(reconstructed));
    CHECK(service.launch_schema_version == 2U);
    CHECK(service.launch_sequence == 3U);
    CHECK(memcmp(launch_snapshot, reconstructed,
                 sizeof(reconstructed)) == 0);
    p4_game_save_service_clear(&service);
    free(queue_workspace);
    free(object_workspace);
    free(launch_snapshot);
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
        test_legacy_marker_t marker = {0};
        const p4_game_save_legacy_policy_t legacy_policy =
            test_legacy_policy(&marker);
        CHECK(p4_game_save_store_init(
            &store, root, P4_GAME_SAVE_STORAGE_WRITABLE, &s_protection,
            &legacy_policy));
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
        bool authenticated = false;
        CHECK(p4_game_save_store_load(
            &store, "org.p4console.lord", "AUTO", workspace,
            P4_GAME_SAVE_MAX_FILE_BYTES, loaded, sizeof(loaded),
            &loaded_bytes, &schema, &sequence, &authenticated) ==
              P4_GAME_SAVE_STORE_OK);
        CHECK(authenticated);
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
    test_legacy_marker_t marker = {0};
    const p4_game_save_legacy_policy_t legacy_policy =
        test_legacy_policy(&marker);
    CHECK(p4_game_save_service_init(
        &service, root, P4_GAME_SAVE_STORAGE_WRITABLE,
        &s_protection, &legacy_policy, "org.p4console.lord", "AUTO",
        queue_workspace,
        P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES, object_workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, launch_snapshot,
        P4_GAME_SAVE_MAX_BYTES));
    CHECK(p4_game_save_service_available(&service));
    CHECK(marker.closed);
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
            &s_protection, &legacy_policy, "org.p4console.lord", "AUTO",
            queue_workspace2,
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

        marker.fail_object_advance = true;
        const uint8_t marker_failure[] = {6U};
        p4_game_save_ticket_t marker_ticket =
            P4_GAME_SAVE_INVALID_TICKET;
        CHECK(p4_game_save_service_queue(
            &reloaded, "AUTO", 1U, 1U, marker_failure,
            sizeof(marker_failure), &marker_ticket));
        CHECK(p4_game_save_service_process(&reloaded, 1U) == 1U);
        CHECK(p4_game_save_service_read_status(
            &reloaded, marker_ticket, &status, &sequence));
        CHECK(status == P4_GAME_SAVE_ERROR && sequence == 1U);
        CHECK(!p4_game_save_service_available(&reloaded));
        marker.fail_object_advance = false;
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

static void test_legacy_migration(void)
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

    char saves[384];
    char game_directory[384];
    char current[384];
    (void)snprintf(saves, sizeof(saves), "%s/SAVES", root);
    (void)snprintf(game_directory, sizeof(game_directory),
                   "%s/SAVES/org.p4console.lord", root);
    (void)snprintf(current, sizeof(current),
                   "%s/AUTO.P4SAVE", game_directory);
    CHECK(mkdir(saves, 0700) == 0);
    CHECK(mkdir(game_directory, 0700) == 0);

    const uint8_t legacy_payload[] = {0xc0U, 0xffU, 0xeeU};
    size_t legacy_bytes = 0U;
    CHECK(p4_game_save_encode(
        "org.p4console.lord", "AUTO", 2U, 7U, legacy_payload,
        sizeof(legacy_payload), object_workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, &legacy_bytes) == P4_GAME_SAVE_VALID);
    FILE *file = fopen(current, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(fwrite(object_workspace, 1U, legacy_bytes, file) ==
              legacy_bytes);
        CHECK(fclose(file) == 0);
    }

    p4_game_save_service_t service;
    memset(&service, 0, sizeof(service));
    test_legacy_marker_t marker = {0};
    const p4_game_save_legacy_policy_t legacy_policy =
        test_legacy_policy(&marker);
    CHECK(p4_game_save_service_init(
        &service, root, P4_GAME_SAVE_STORAGE_WRITABLE, &s_protection,
        &legacy_policy, "org.p4console.lord", "AUTO", queue_workspace,
        P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES, object_workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, launch_snapshot,
        P4_GAME_SAVE_MAX_BYTES));
    CHECK(p4_game_save_service_available(&service));
    CHECK(service.launch_migrated_legacy);
    CHECK(marker.closed);
    CHECK(service.launch_schema_version == 2U);
    CHECK(service.launch_sequence == 8U);
    CHECK(service.launch_snapshot_bytes == sizeof(legacy_payload));
    CHECK(memcmp(launch_snapshot, legacy_payload,
                 sizeof(legacy_payload)) == 0);

    file = fopen(current, "rb");
    CHECK(file != NULL);
    size_t sealed_bytes = 0U;
    if (file != NULL) {
        sealed_bytes = fread(
            object_workspace, 1U, P4_GAME_SAVE_MAX_FILE_BYTES, file);
        CHECK(ferror(file) == 0);
        CHECK(fclose(file) == 0);
    }
    p4_game_save_info_t info;
    CHECK(p4_game_save_parse_authenticated(
        &s_protection, object_workspace, sealed_bytes,
        "org.p4console.lord", "AUTO", &info) == P4_GAME_SAVE_VALID);
    CHECK(info.authenticated && info.sequence == 8U);
    CHECK(p4_game_save_parse_authenticated(
        &s_other_protection, object_workspace, sealed_bytes,
        "org.p4console.lord", "AUTO", &info) == P4_GAME_SAVE_BAD_AUTH);

    char backup[384];
    (void)snprintf(backup, sizeof(backup),
                   "%s/AUTO.P4SAVE.BAK", game_directory);
    CHECK(access(backup, F_OK) != 0);
    p4_game_save_service_clear(&service);

    /* Once the NVS-backed marker is closed, neither a replaced current nor
     * an injected backup may be grandfathered and re-signed. */
    const uint8_t forged_payload[] = {0xffU, 0xffU, 0xffU, 0xffU};
    CHECK(p4_game_save_encode(
        "org.p4console.lord", "AUTO", 99U, 900U, forged_payload,
        sizeof(forged_payload), object_workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, &legacy_bytes) == P4_GAME_SAVE_VALID);
    const char *const forged_paths[] = {current, backup};
    for (size_t index = 0U;
         index < sizeof(forged_paths) / sizeof(forged_paths[0]); ++index) {
        file = fopen(forged_paths[index], "wb");
        CHECK(file != NULL);
        if (file != NULL) {
            CHECK(fwrite(object_workspace, 1U, legacy_bytes, file) ==
                  legacy_bytes);
            CHECK(fclose(file) == 0);
        }
    }
    memset(&service, 0, sizeof(service));
    CHECK(p4_game_save_service_init(
        &service, root, P4_GAME_SAVE_STORAGE_WRITABLE, &s_protection,
        &legacy_policy, "org.p4console.lord", "AUTO", queue_workspace,
        P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES, object_workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, launch_snapshot,
        P4_GAME_SAVE_MAX_BYTES));
    CHECK(!p4_game_save_service_available(&service));
    CHECK(service.startup_result == P4_GAME_SAVE_STORE_CORRUPT);
    CHECK(!service.launch_migrated_legacy);
    CHECK(marker.closed);
    uint8_t current_magic[8] = {0};
    file = fopen(current, "rb");
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(fread(current_magic, 1U, sizeof(current_magic), file) ==
              sizeof(current_magic));
        CHECK(fclose(file) == 0);
    }
    CHECK(memcmp(current_magic, P4_GAME_SAVE_MAGIC,
                 sizeof(current_magic)) == 0);
    p4_game_save_service_clear(&service);
    free(queue_workspace);
    free(object_workspace);
    free(launch_snapshot);
    cleanup_temp_root(root);
}

static void test_legacy_marker_failure_is_closed(void)
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

    test_legacy_marker_t marker = {.fail_close = true};
    const p4_game_save_legacy_policy_t legacy_policy =
        test_legacy_policy(&marker);
    p4_game_save_service_t service;
    CHECK(p4_game_save_service_init(
        &service, root, P4_GAME_SAVE_STORAGE_WRITABLE, &s_protection,
        &legacy_policy, "org.p4console.lord", "AUTO", queue_workspace,
        P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES, object_workspace,
        P4_GAME_SAVE_MAX_FILE_BYTES, launch_snapshot,
        P4_GAME_SAVE_MAX_BYTES));
    CHECK(!p4_game_save_service_available(&service));
    CHECK(service.startup_result == P4_GAME_SAVE_STORE_IO_ERROR);
    CHECK(!marker.closed);
    const uint8_t payload[] = {1U};
    p4_game_save_ticket_t ticket = P4_GAME_SAVE_INVALID_TICKET;
    CHECK(!p4_game_save_service_queue(
        &service, "AUTO", 1U, 0U, payload, sizeof(payload), &ticket));
    p4_game_save_service_clear(&service);
    free(queue_workspace);
    free(object_workspace);
    free(launch_snapshot);
    cleanup_temp_root(root);
}

int main(void)
{
    test_container();
    test_authenticated_container();
    test_maximum_container();
    test_memory_backend();
    test_store_round_trip();
    test_authenticated_baseline_is_anchored();
    test_same_sequence_alternate_branch_is_rejected();
    test_missing_files_resume_from_anchor_floor();
    test_store_power_loss();
    test_durable_service();
    test_legacy_migration();
    test_legacy_marker_failure_is_closed();
    if (s_failures != 0) {
        fprintf(stderr, "%d save test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("p4 game save tests passed");
    return EXIT_SUCCESS;
}
