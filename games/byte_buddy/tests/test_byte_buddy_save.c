// SPDX-License-Identifier: MIT

#include "byte_buddy_save.h"

#include <stdio.h>
#include <string.h>

static unsigned s_failures;

#define CHECK(condition) do {                                             \
    if (!(condition)) {                                                   \
        fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                __FILE__, __LINE__, #condition);                          \
        ++s_failures;                                                     \
    }                                                                     \
} while (0)

static byte_buddy_save_profile_t sample_profile(void)
{
    return (byte_buddy_save_profile_t){
        .hunger = 61U,
        .joy = 72U,
        .hygiene = 83U,
        .energy = 94U,
        .coins = 321U,
        .care_actions = 144U,
        .style_mix_count = 19U,
        .pet_actions = 11U,
        .action_counts = {17U, 23U, 31U, 37U},
        .upgrades = {3U, 2U, 1U, 0U},
        .style_unlocked = {7U, 5U, 4U, 3U},
        .style_selected = {6U, 4U, 3U, 2U},
        .achievement_mask = UINT32_C(0x0d),
    };
}

static void test_round_trip(void)
{
    const byte_buddy_save_profile_t source = sample_profile();
    uint8_t payload[BYTE_BUDDY_SAVE_PAYLOAD_BYTES];
    CHECK(byte_buddy_save_encode(&source, payload, sizeof(payload)) ==
          sizeof(payload));
    byte_buddy_save_profile_t restored = {0};
    CHECK(byte_buddy_save_decode(&restored, payload, sizeof(payload)));
    CHECK(source.hunger == restored.hunger);
    CHECK(source.joy == restored.joy);
    CHECK(source.hygiene == restored.hygiene);
    CHECK(source.energy == restored.energy);
    CHECK(source.coins == restored.coins);
    CHECK(source.care_actions == restored.care_actions);
    CHECK(source.style_mix_count == restored.style_mix_count);
    CHECK(source.pet_actions == restored.pet_actions);
    CHECK(memcmp(source.action_counts, restored.action_counts,
                 sizeof(source.action_counts)) == 0);
    CHECK(memcmp(source.upgrades, restored.upgrades,
                 sizeof(source.upgrades)) == 0);
    CHECK(memcmp(source.style_unlocked, restored.style_unlocked,
                 sizeof(source.style_unlocked)) == 0);
    CHECK(memcmp(source.style_selected, restored.style_selected,
                 sizeof(source.style_selected)) == 0);
    CHECK(source.achievement_mask == restored.achievement_mask);
}

static void test_rejects_invalid_payloads(void)
{
    byte_buddy_save_profile_t source = sample_profile();
    uint8_t payload[BYTE_BUDDY_SAVE_PAYLOAD_BYTES];
    CHECK(byte_buddy_save_encode(&source, payload, sizeof(payload)) ==
          sizeof(payload));
    byte_buddy_save_profile_t restored = {0};
    CHECK(!byte_buddy_save_decode(&restored, payload, sizeof(payload) - 1U));
    CHECK(!byte_buddy_save_decode(&restored, payload, sizeof(payload) + 1U));
    payload[20U] ^= UINT8_C(0x40);
    CHECK(!byte_buddy_save_decode(&restored, payload, sizeof(payload)));

    source.hunger = 101U;
    CHECK(byte_buddy_save_encode(&source, payload, sizeof(payload)) == 0U);
    source = sample_profile();
    source.upgrades[2] = 4U;
    CHECK(byte_buddy_save_encode(&source, payload, sizeof(payload)) == 0U);
    source = sample_profile();
    source.style_selected[1] = 6U;
    CHECK(byte_buddy_save_encode(&source, payload, sizeof(payload)) == 0U);
    source = sample_profile();
    source.achievement_mask = UINT32_C(0x10);
    CHECK(byte_buddy_save_encode(&source, payload, sizeof(payload)) == 0U);
    source = sample_profile();
    source.care_actions = 103U;
    CHECK(byte_buddy_save_encode(&source, payload, sizeof(payload)) == 0U);
    CHECK(byte_buddy_save_encode(&source, payload, sizeof(payload) - 1U) ==
          0U);
}

static bool contains_bytes(const uint8_t *haystack, size_t haystack_bytes,
                           const uint8_t *needle, size_t needle_bytes)
{
    if (needle_bytes == 0U || needle_bytes > haystack_bytes) {
        return false;
    }
    for (size_t offset = 0U;
         offset <= haystack_bytes - needle_bytes; ++offset) {
        if (memcmp(haystack + offset, needle, needle_bytes) == 0) {
            return true;
        }
    }
    return false;
}

static void test_payload_has_no_signal_identity_fields(void)
{
    const byte_buddy_save_profile_t source = sample_profile();
    uint8_t payload[BYTE_BUDDY_SAVE_PAYLOAD_BYTES];
    CHECK(byte_buddy_save_encode(&source, payload, sizeof(payload)) ==
          sizeof(payload));
    static const uint8_t forbidden_label[] = "SECRET_WIFI_NAME";
    static const uint8_t forbidden_token[] = {
        0xdeU, 0xadU, 0xbeU, 0xefU, 0x18U, 0x42U, 0x67U, 0x99U,
    };
    CHECK(!contains_bytes(payload, sizeof(payload), forbidden_label,
                          sizeof(forbidden_label) - 1U));
    CHECK(!contains_bytes(payload, sizeof(payload), forbidden_token,
                          sizeof(forbidden_token)));
}

int main(void)
{
    test_round_trip();
    test_rejects_invalid_payloads();
    test_payload_has_no_signal_identity_fields();
    if (s_failures != 0U) {
        fprintf(stderr, "byte buddy save tests failed: %u\n", s_failures);
        return 1;
    }
    puts("byte buddy save tests passed");
    return 0;
}
