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

static const uint8_t s_historical_schema_1_payload[
    BYTE_BUDDY_SAVE_PAYLOAD_BYTES] = {
    0x42U, 0x42U, 0x53U, 0x41U, 0x56U, 0x45U, 0x31U, 0x00U,
    0x01U, 0x00U, 0x38U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x49U, 0x40U, 0x5bU, 0x34U, 0x45U, 0x23U, 0x68U, 0x00U,
    0x56U, 0x34U, 0x67U, 0x45U, 0x02U, 0x01U, 0x04U, 0x03U,
    0x06U, 0x05U, 0x08U, 0x07U, 0x00U, 0x01U, 0x02U, 0x03U,
    0x07U, 0x05U, 0x04U, 0x04U, 0x06U, 0x04U, 0x03U, 0x02U,
    0x0fU, 0x00U, 0x00U, 0x00U, 0x95U, 0xe3U, 0xacU, 0xe4U,
};

static byte_buddy_save_profile_t historical_schema_1_profile(void)
{
    byte_buddy_save_profile_t profile;
    /* Keep a recognizable sentinel in any implementation padding. The
     * encoder must serialize only the explicitly assigned durable fields. */
    memset(&profile, 0xa5, sizeof(profile));
    profile.hunger = 73U;
    profile.joy = 64U;
    profile.hygiene = 91U;
    profile.energy = 52U;
    profile.coins = UINT16_C(0x2345);
    profile.care_actions = 104U;
    profile.style_mix_count = UINT16_C(0x3456);
    profile.pet_actions = UINT16_C(0x4567);
    profile.action_counts[0] = UINT16_C(0x0102);
    profile.action_counts[1] = UINT16_C(0x0304);
    profile.action_counts[2] = UINT16_C(0x0506);
    profile.action_counts[3] = UINT16_C(0x0708);
    profile.upgrades[0] = 0U;
    profile.upgrades[1] = 1U;
    profile.upgrades[2] = 2U;
    profile.upgrades[3] = 3U;
    profile.style_unlocked[0] = 7U;
    profile.style_unlocked[1] = 5U;
    profile.style_unlocked[2] = 4U;
    profile.style_unlocked[3] = 4U;
    profile.style_selected[0] = 6U;
    profile.style_selected[1] = 4U;
    profile.style_selected[2] = 3U;
    profile.style_selected[3] = 2U;
    profile.achievement_mask = UINT32_C(0x0f);
    return profile;
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

static void test_historical_schema_1_vector(void)
{
    CHECK(sizeof(s_historical_schema_1_payload) == 56U);
    byte_buddy_save_profile_t decoded = {0};
    CHECK(byte_buddy_save_decode(
        &decoded, s_historical_schema_1_payload,
        sizeof(s_historical_schema_1_payload)));
    CHECK(decoded.hunger == 73U);
    CHECK(decoded.joy == 64U);
    CHECK(decoded.hygiene == 91U);
    CHECK(decoded.energy == 52U);
    CHECK(decoded.coins == UINT16_C(0x2345));
    CHECK(decoded.care_actions == 104U);
    CHECK(decoded.style_mix_count == UINT16_C(0x3456));
    CHECK(decoded.pet_actions == UINT16_C(0x4567));
    CHECK(decoded.action_counts[0] == UINT16_C(0x0102));
    CHECK(decoded.action_counts[1] == UINT16_C(0x0304));
    CHECK(decoded.action_counts[2] == UINT16_C(0x0506));
    CHECK(decoded.action_counts[3] == UINT16_C(0x0708));
    CHECK(decoded.upgrades[0] == 0U);
    CHECK(decoded.upgrades[1] == 1U);
    CHECK(decoded.upgrades[2] == 2U);
    CHECK(decoded.upgrades[3] == 3U);
    CHECK(decoded.style_unlocked[0] == 7U);
    CHECK(decoded.style_unlocked[1] == 5U);
    CHECK(decoded.style_unlocked[2] == 4U);
    CHECK(decoded.style_unlocked[3] == 4U);
    CHECK(decoded.style_selected[0] == 6U);
    CHECK(decoded.style_selected[1] == 4U);
    CHECK(decoded.style_selected[2] == 3U);
    CHECK(decoded.style_selected[3] == 2U);
    CHECK(decoded.achievement_mask == UINT32_C(0x0f));

    const byte_buddy_save_profile_t source =
        historical_schema_1_profile();
    uint8_t encoded[BYTE_BUDDY_SAVE_PAYLOAD_BYTES];
    memset(encoded, 0xa5, sizeof(encoded));
    CHECK(byte_buddy_save_encode(
        &source, encoded, sizeof(encoded)) == sizeof(encoded));
    CHECK(memcmp(encoded, s_historical_schema_1_payload,
                 sizeof(encoded)) == 0);
    static const uint8_t privacy_sentinel[] = {
        0xa5U, 0xa5U, 0xa5U, 0xa5U,
    };
    CHECK(!contains_bytes(encoded, sizeof(encoded), privacy_sentinel,
                          sizeof(privacy_sentinel)));
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
    test_historical_schema_1_vector();
    test_payload_has_no_signal_identity_fields();
    if (s_failures != 0U) {
        fprintf(stderr, "byte buddy save tests failed: %u\n", s_failures);
        return 1;
    }
    puts("byte buddy save tests passed");
    return 0;
}
