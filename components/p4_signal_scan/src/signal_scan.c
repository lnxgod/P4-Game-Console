// SPDX-License-Identifier: MIT

#include "p4/signal_scan.h"

#include <string.h>

static uint64_t rotate_left(uint64_t value, unsigned bits)
{
    return (value << bits) | (value >> (64U - bits));
}

static uint64_t read_u64_le(const uint8_t *data)
{
    uint64_t value = 0U;
    for (unsigned index = 0U; index < 8U; ++index) {
        value |= (uint64_t)data[index] << (index * 8U);
    }
    return value;
}

#define SIP_ROUND() do { \
        v0 += v1; v1 = rotate_left(v1, 13U); v1 ^= v0; \
        v0 = rotate_left(v0, 32U); \
        v2 += v3; v3 = rotate_left(v3, 16U); v3 ^= v2; \
        v0 += v3; v3 = rotate_left(v3, 21U); v3 ^= v0; \
        v2 += v1; v1 = rotate_left(v1, 17U); v1 ^= v2; \
        v2 = rotate_left(v2, 32U); \
    } while (0)

static uint64_t siphash24(const uint8_t key[16],
                          const uint8_t *message, size_t bytes)
{
    const uint64_t k0 = read_u64_le(key);
    const uint64_t k1 = read_u64_le(key + 8U);
    uint64_t v0 = UINT64_C(0x736f6d6570736575) ^ k0;
    uint64_t v1 = UINT64_C(0x646f72616e646f6d) ^ k1;
    uint64_t v2 = UINT64_C(0x6c7967656e657261) ^ k0;
    uint64_t v3 = UINT64_C(0x7465646279746573) ^ k1;
    const size_t blocks = bytes / 8U;
    for (size_t block = 0U; block < blocks; ++block) {
        const uint64_t word = read_u64_le(message + block * 8U);
        v3 ^= word;
        SIP_ROUND();
        SIP_ROUND();
        v0 ^= word;
    }
    uint64_t tail = (uint64_t)bytes << 56U;
    const size_t remaining = bytes - blocks * 8U;
    for (size_t index = 0U; index < remaining; ++index) {
        tail |= (uint64_t)message[blocks * 8U + index] << (index * 8U);
    }
    v3 ^= tail;
    SIP_ROUND();
    SIP_ROUND();
    v0 ^= tail;
    v2 ^= UINT64_C(0xff);
    SIP_ROUND();
    SIP_ROUND();
    SIP_ROUND();
    SIP_ROUND();
    return v0 ^ v1 ^ v2 ^ v3;
}

#undef SIP_ROUND

static void sanitize_label(const uint8_t *ssid, size_t ssid_bytes,
                           char out[P4_GAME_SIGNAL_LABEL_MAX_BYTES])
{
    if (ssid_bytes == 0U) {
        (void)memcpy(out, "HIDDEN SIGNAL", sizeof("HIDDEN SIGNAL"));
        return;
    }
    size_t output = 0U;
    for (size_t index = 0U;
         index < ssid_bytes && output + 1U < P4_GAME_SIGNAL_LABEL_MAX_BYTES;
         ++index) {
        const uint8_t value = ssid[index];
        out[output++] = value >= UINT8_C(0x20) && value <= UINT8_C(0x7e)
            ? (char)value : '?';
    }
    while (output > 0U && out[output - 1U] == ' ') {
        --output;
    }
    if (output == 0U) {
        (void)memcpy(out, "HIDDEN SIGNAL", sizeof("HIDDEN SIGNAL"));
        return;
    }
    out[output] = '\0';
}

bool p4_signal_scan_make_game_signal(
    const uint8_t key[P4_SIGNAL_SCAN_KEY_BYTES],
    const uint8_t bssid[P4_SIGNAL_SCAN_BSSID_BYTES],
    const uint8_t *ssid,
    size_t ssid_bytes,
    int8_t rssi_dbm,
    uint8_t channel,
    bool protected_network,
    p4_game_signal_t *out_signal)
{
    if (key == NULL || bssid == NULL || out_signal == NULL ||
        ssid_bytes > P4_SIGNAL_SCAN_SSID_MAX_BYTES ||
        (ssid_bytes != 0U && ssid == NULL) || rssi_dbm > 0 ||
        channel > 196U) {
        return false;
    }
    uint8_t identity[1U + P4_SIGNAL_SCAN_BSSID_BYTES +
                     P4_SIGNAL_SCAN_SSID_MAX_BYTES] = {UINT8_C(0x53)};
    (void)memcpy(identity + 1U, bssid, P4_SIGNAL_SCAN_BSSID_BYTES);
    if (ssid_bytes != 0U) {
        (void)memcpy(identity + 1U + P4_SIGNAL_SCAN_BSSID_BYTES,
                     ssid, ssid_bytes);
    }
    uint64_t token = siphash24(
        key, identity, 1U + P4_SIGNAL_SCAN_BSSID_BYTES + ssid_bytes);
    if (token == 0U) {
        token = 1U;
    }
    *out_signal = (p4_game_signal_t){
        .token = token,
        .rssi_dbm = rssi_dbm,
        .channel = channel,
        .flags = (uint8_t)(
            (ssid_bytes == 0U ? P4_GAME_SIGNAL_HIDDEN : 0U) |
            (protected_network ? P4_GAME_SIGNAL_PROTECTED : 0U)),
    };
    sanitize_label(ssid, ssid_bytes, out_signal->label);
    return true;
}

void p4_signal_scan_clear_results(p4_game_signal_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    snapshot->count = 0U;
    (void)memset(snapshot->results, 0, sizeof(snapshot->results));
}

size_t p4_signal_scan_select_window(
    const p4_game_signal_t *candidates,
    size_t candidate_count,
    size_t window_cursor,
    uint64_t focus_token,
    p4_game_signal_t *out_results,
    size_t *next_window_cursor)
{
    if (next_window_cursor == NULL) {
        return 0U;
    }
    *next_window_cursor = 0U;
    if (out_results != NULL) {
        (void)memset(out_results, 0,
                     sizeof(*out_results) * P4_GAME_SIGNAL_MAX_RESULTS);
    }
    if (out_results == NULL || candidate_count > P4_SIGNAL_SCAN_MAX_CANDIDATES ||
        (candidate_count != 0U && candidates == NULL)) {
        return 0U;
    }
    if (candidate_count == 0U) {
        return 0U;
    }

    const size_t start = window_cursor % candidate_count;
    size_t output_count = 0U;
    if (focus_token != 0U) {
        for (size_t index = 0U; index < candidate_count; ++index) {
            if (candidates[index].token == focus_token) {
                out_results[output_count++] = candidates[index];
                break;
            }
        }
    }

    size_t inspected = 0U;
    while (inspected < candidate_count &&
           output_count < P4_GAME_SIGNAL_MAX_RESULTS) {
        const size_t index = (start + inspected) % candidate_count;
        ++inspected;
        if (focus_token != 0U &&
            candidates[index].token == focus_token) {
            continue;
        }
        out_results[output_count++] = candidates[index];
    }
    *next_window_cursor = (start + inspected) % candidate_count;
    return output_count;
}
