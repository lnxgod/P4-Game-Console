// SPDX-License-Identifier: MIT

#include "platform/save_seal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "fake_esp.h"
#include "mbedtls/sha256.h"

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static bool all_zero(const uint8_t *data, size_t bytes)
{
    uint8_t combined = 0U;
    for (size_t index = 0U; index < bytes; ++index) {
        combined |= data[index];
    }
    return combined == 0U;
}

static void write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static void test_paired_initialization(void)
{
    fake_nvs_reset();
    uint8_t key[PLATFORM_SAVE_SEAL_KEY_BYTES] = {0};
    CHECK(platform_save_seal_load_key(key) == ESP_OK);
    CHECK(!all_zero(key, sizeof(key)));
    CHECK(fake_random_call_count() == 1U);
    CHECK(fake_nvs_commit_count() == 1U);
    CHECK(fake_nvs_committed_present("master_v1"));
    CHECK(fake_nvs_committed_bytes("master_v1") == sizeof(key));
    CHECK(fake_nvs_committed_present("legacy_v1"));
    CHECK(fake_nvs_committed_bytes("legacy_v1") == 1176U);

    uint8_t reloaded[PLATFORM_SAVE_SEAL_KEY_BYTES] = {0};
    CHECK(platform_save_seal_load_key(reloaded) == ESP_OK);
    CHECK(memcmp(key, reloaded, sizeof(key)) == 0);
    CHECK(fake_random_call_count() == 1U);
    CHECK(fake_nvs_commit_count() == 1U);

    bool closed = true;
    CHECK(platform_save_seal_legacy_is_closed(
        "org.p4console.lord", "AUTO", &closed) == ESP_OK);
    CHECK(!closed);
    CHECK(platform_save_seal_close_legacy(
        "org.p4console.lord", "AUTO") == ESP_OK);
    CHECK(platform_save_seal_legacy_is_closed(
        "org.p4console.lord", "AUTO", &closed) == ESP_OK);
    CHECK(closed);
    platform_save_seal_clear(key, sizeof(key));
    platform_save_seal_clear(reloaded, sizeof(reloaded));
}

static void test_v1_registry_migration(void)
{
    uint8_t key[PLATFORM_SAVE_SEAL_KEY_BYTES] = {0};
    fake_nvs_reset();
    CHECK(platform_save_seal_load_key(key) == ESP_OK);
    uint8_t old_registry[1048] = {0};
    memcpy(old_registry, "P4LMRK1", 7U);
    write_u32(old_registry + 8U, sizeof(old_registry));
    write_u32(old_registry + 12U, 1U);
    write_u32(old_registry + 16U, 1U);
    static const uint8_t domain[] = "P4SAVE2-LEGACY-CLOSED";
    static const char game_id[] = "org.p4console.lord";
    static const char slot_id[] = "AUTO";
    uint8_t material[sizeof(domain) + sizeof(game_id) + sizeof(slot_id)];
    size_t used = 0U;
    memcpy(material + used, domain, sizeof(domain));
    used += sizeof(domain);
    memcpy(material + used, game_id, sizeof(game_id));
    used += sizeof(game_id);
    memcpy(material + used, slot_id, sizeof(slot_id));
    used += sizeof(slot_id);
    CHECK(mbedtls_sha256(
        material, used, old_registry + 24U, 0) == 0);
    fake_nvs_replace_committed(
        "legacy_v1", old_registry, sizeof(old_registry));
    uint8_t reloaded[PLATFORM_SAVE_SEAL_KEY_BYTES] = {0};
    CHECK(platform_save_seal_load_key(reloaded) == ESP_OK);
    CHECK(memcmp(key, reloaded, sizeof(key)) == 0);
    CHECK(fake_nvs_committed_bytes("legacy_v1") == 1176U);
    CHECK(fake_nvs_commit_count() == 2U);
    uint32_t sequence = 99U;
    CHECK(platform_save_seal_object_sequence(
        game_id, slot_id, &sequence) == ESP_ERR_INVALID_STATE);
    CHECK(sequence == 0U);
    uint8_t object_sha[PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES];
    memset(object_sha, 0x35, sizeof(object_sha));
    bool allowed = false;
    CHECK(platform_save_seal_object_is_allowed(
        game_id, slot_id, 7U, object_sha, &allowed) == ESP_OK);
    CHECK(allowed);
    CHECK(platform_save_seal_advance_object(
        game_id, slot_id, 7U, object_sha) == ESP_OK);
    CHECK(platform_save_seal_object_sequence(
        game_id, slot_id, &sequence) == ESP_OK);
    CHECK(sequence == 7U);
    platform_save_seal_clear(key, sizeof(key));
    platform_save_seal_clear(reloaded, sizeof(reloaded));
}

static void test_object_freshness_anchor(void)
{
    uint8_t key[PLATFORM_SAVE_SEAL_KEY_BYTES] = {0};
    uint8_t object_a[PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES];
    uint8_t object_b[PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES];
    memset(object_a, 0x2a, sizeof(object_a));
    memset(object_b, 0x7b, sizeof(object_b));
    fake_nvs_reset();
    CHECK(platform_save_seal_load_key(key) == ESP_OK);

    bool allowed = false;
    CHECK(platform_save_seal_object_is_allowed(
        "org.p4console.lord", "AUTO", 5U, object_a,
        &allowed) == ESP_OK);
    CHECK(allowed);
    CHECK(platform_save_seal_advance_object(
        "org.p4console.lord", "AUTO", 5U, object_a) == ESP_OK);
    CHECK(fake_nvs_commit_count() == 2U);
    uint32_t sequence = 0U;
    CHECK(platform_save_seal_object_sequence(
        "org.p4console.lord", "AUTO", &sequence) == ESP_OK);
    CHECK(sequence == 5U);
    CHECK(platform_save_seal_object_is_allowed(
        "org.p4console.lord", "AUTO", 4U, object_a,
        &allowed) == ESP_OK);
    CHECK(!allowed);
    CHECK(platform_save_seal_object_is_allowed(
        "org.p4console.lord", "AUTO", 5U, object_a,
        &allowed) == ESP_OK);
    CHECK(allowed);
    CHECK(platform_save_seal_object_is_allowed(
        "org.p4console.lord", "AUTO", 5U, object_b,
        &allowed) == ESP_OK);
    CHECK(!allowed);
    CHECK(platform_save_seal_object_is_allowed(
        "org.p4console.lord", "AUTO", 6U, object_b,
        &allowed) == ESP_OK);
    CHECK(allowed);
    CHECK(platform_save_seal_advance_object(
        "org.p4console.lord", "AUTO", 5U, object_b) ==
          ESP_ERR_INVALID_STATE);
    CHECK(platform_save_seal_advance_object(
        "org.p4console.lord", "AUTO", 4U, object_a) ==
          ESP_ERR_INVALID_STATE);
    CHECK(platform_save_seal_advance_object(
        "org.p4console.lord", "AUTO", 6U, object_b) == ESP_OK);
    CHECK(fake_nvs_commit_count() == 3U);
    CHECK(platform_save_seal_object_sequence(
        "org.p4console.lord", "AUTO", &sequence) == ESP_OK);
    CHECK(sequence == 6U);
    CHECK(platform_save_seal_object_is_allowed(
        "org.p4console.lord", "AUTO", 5U, object_a,
        &allowed) == ESP_OK);
    CHECK(!allowed);

    /* The registry says this slot has an anchor. Deleting just the per-slot
     * record must fail closed rather than reopening baseline migration. */
    CHECK(fake_nvs_delete_committed_prefix("h") == 1U);
    CHECK(platform_save_seal_object_is_allowed(
        "org.p4console.lord", "AUTO", 99U, object_a,
        &allowed) == ESP_ERR_INVALID_STATE);
    CHECK(!allowed);
    CHECK(platform_save_seal_object_sequence(
        "org.p4console.lord", "AUTO", &sequence) ==
          ESP_ERR_INVALID_STATE);
    platform_save_seal_clear(key, sizeof(key));
}

static void test_partial_namespace_loss_fails_closed(void)
{
    uint8_t key[PLATFORM_SAVE_SEAL_KEY_BYTES] = {0};
    fake_nvs_reset();
    CHECK(platform_save_seal_load_key(key) == ESP_OK);
    fake_nvs_delete_committed("legacy_v1");
    memset(key, 0xa5, sizeof(key));
    CHECK(platform_save_seal_load_key(key) == ESP_ERR_INVALID_STATE);
    CHECK(all_zero(key, sizeof(key)));
    CHECK(fake_random_call_count() == 1U);
    bool closed = false;
    CHECK(platform_save_seal_legacy_is_closed(
        "org.p4console.lord", "AUTO", &closed) == ESP_ERR_INVALID_STATE);

    fake_nvs_reset();
    CHECK(platform_save_seal_load_key(key) == ESP_OK);
    fake_nvs_delete_committed("master_v1");
    const unsigned random_before = fake_random_call_count();
    CHECK(platform_save_seal_load_key(key) == ESP_ERR_INVALID_STATE);
    CHECK(all_zero(key, sizeof(key)));
    CHECK(fake_random_call_count() == random_before);
}

static void test_registry_shape_and_capacity(void)
{
    uint8_t key[PLATFORM_SAVE_SEAL_KEY_BYTES] = {0};
    fake_nvs_reset();
    CHECK(platform_save_seal_load_key(key) == ESP_OK);
    for (unsigned index = 0U; index < 32U; ++index) {
        char game_id[32];
        const int written = snprintf(
            game_id, sizeof(game_id), "org.p4console.test%u", index);
        CHECK(written > 0 && (size_t)written < sizeof(game_id));
        CHECK(platform_save_seal_close_legacy(game_id, "AUTO") == ESP_OK);
    }
    CHECK(platform_save_seal_close_legacy(
        "org.p4console.overflow", "AUTO") == ESP_ERR_NO_MEM);
    CHECK(platform_save_seal_close_legacy(
        "org.p4console.test0", "AUTO") == ESP_OK);

    const uint8_t malformed[] = {1U, 2U, 3U};
    fake_nvs_replace_committed(
        "legacy_v1", malformed, sizeof(malformed));
    memset(key, 0xa5, sizeof(key));
    CHECK(platform_save_seal_load_key(key) == ESP_ERR_INVALID_STATE);
    CHECK(all_zero(key, sizeof(key)));
}

int main(void)
{
    test_paired_initialization();
    test_v1_registry_migration();
    test_object_freshness_anchor();
    test_partial_namespace_loss_fails_closed();
    test_registry_shape_and_capacity();
    if (s_failures != 0) {
        fprintf(stderr, "%d platform save seal failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("platform save seal tests passed");
    return EXIT_SUCCESS;
}
