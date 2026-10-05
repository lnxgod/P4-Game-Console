// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p4/signal_scan.h"

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

int main(void)
{
    const uint8_t key[P4_SIGNAL_SCAN_KEY_BYTES] = {
        0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U,
        8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U,
    };
    uint8_t other_key[P4_SIGNAL_SCAN_KEY_BYTES];
    (void)memset(other_key, UINT8_C(0xa5), sizeof(other_key));
    const uint8_t bssid[P4_SIGNAL_SCAN_BSSID_BYTES] = {
        UINT8_C(0x02), UINT8_C(0x10), UINT8_C(0x20),
        UINT8_C(0x30), UINT8_C(0x40), UINT8_C(0x50),
    };
    const uint8_t ssid[] = {'S', 'K', 'Y', '\n', UINT8_C(0xff), ' ', ' '};
    p4_game_signal_t first;
    p4_game_signal_t repeat;
    p4_game_signal_t rekeyed;
    CHECK(p4_signal_scan_make_game_signal(
        key, bssid, ssid, sizeof(ssid), -61, 6U, true, &first));
    CHECK(strcmp(first.label, "SKY??") == 0);
    CHECK(first.token != 0U);
    CHECK(first.rssi_dbm == -61);
    CHECK(first.channel == 6U);
    CHECK(first.flags == P4_GAME_SIGNAL_PROTECTED);
    CHECK(p4_signal_scan_make_game_signal(
        key, bssid, ssid, sizeof(ssid), -45, 6U, true, &repeat));
    CHECK(first.token == repeat.token);
    CHECK(p4_signal_scan_make_game_signal(
        other_key, bssid, ssid, sizeof(ssid), -61, 6U, true, &rekeyed));
    CHECK(first.token != rekeyed.token);

    p4_game_signal_t hidden;
    CHECK(p4_signal_scan_make_game_signal(
        key, bssid, NULL, 0U, -80, 11U, false, &hidden));
    CHECK(strcmp(hidden.label, "HIDDEN SIGNAL") == 0);
    CHECK((hidden.flags & P4_GAME_SIGNAL_HIDDEN) != 0U);
    CHECK(!p4_signal_scan_make_game_signal(
        key, bssid, ssid, P4_SIGNAL_SCAN_SSID_MAX_BYTES + 1U,
        -40, 1U, false, &hidden));
    CHECK(!p4_signal_scan_make_game_signal(
        key, bssid, ssid, sizeof(ssid), 1, 1U, false, &hidden));

    p4_game_signal_snapshot_t stale = {
        .generation = 9U,
        .status = P4_GAME_SIGNAL_ERROR,
        .count = 2U,
        .results = {
            {.token = 41U, .label = "OLD ONE", .rssi_dbm = -45},
            {.token = 42U, .label = "OLD TWO", .rssi_dbm = -55},
        },
    };
    p4_signal_scan_clear_results(&stale);
    CHECK(stale.generation == 9U);
    CHECK(stale.status == P4_GAME_SIGNAL_ERROR);
    CHECK(stale.count == 0U);
    p4_game_signal_t empty_results[P4_GAME_SIGNAL_MAX_RESULTS] = {0};
    CHECK(memcmp(stale.results, empty_results, sizeof(empty_results)) == 0);
    p4_signal_scan_clear_results(NULL);

    p4_game_signal_t candidates[P4_SIGNAL_SCAN_MAX_CANDIDATES] = {0};
    for (size_t index = 0U; index < P4_SIGNAL_SCAN_MAX_CANDIDATES; ++index) {
        candidates[index].token = (uint64_t)index + 1U;
        candidates[index].rssi_dbm = (int8_t)(-30 - (int)index);
    }
    p4_game_signal_t window[P4_GAME_SIGNAL_MAX_RESULTS];
    bool seen[P4_SIGNAL_SCAN_MAX_CANDIDATES] = {false};
    size_t cursor = 0U;
    for (size_t scan = 0U;
         scan < P4_SIGNAL_SCAN_MAX_CANDIDATES /
                    P4_GAME_SIGNAL_MAX_RESULTS;
         ++scan) {
        const size_t selected = p4_signal_scan_select_window(
            candidates, P4_SIGNAL_SCAN_MAX_CANDIDATES, cursor, 0U,
            window, &cursor);
        CHECK(selected == P4_GAME_SIGNAL_MAX_RESULTS);
        for (size_t index = 0U; index < selected; ++index) {
            CHECK(window[index].token >= 1U);
            CHECK(window[index].token <= P4_SIGNAL_SCAN_MAX_CANDIDATES);
            const size_t seen_index = (size_t)(window[index].token - 1U);
            CHECK(!seen[seen_index]);
            seen[seen_index] = true;
        }
    }
    CHECK(cursor == 0U);
    for (size_t index = 0U; index < P4_SIGNAL_SCAN_MAX_CANDIDATES; ++index) {
        CHECK(seen[index]);
    }

    for (size_t candidate_count = 1U;
         candidate_count <= (size_t)P4_SIGNAL_SCAN_MAX_CANDIDATES;
         ++candidate_count) {
        (void)memset(seen, 0, sizeof(seen));
        cursor = candidate_count - 1U;
        for (size_t scan = 0U; scan < candidate_count; ++scan) {
            const size_t selected = p4_signal_scan_select_window(
                candidates, candidate_count, cursor, 0U,
                window, &cursor);
            const size_t expected =
                candidate_count < (size_t)P4_GAME_SIGNAL_MAX_RESULTS
                ? candidate_count : (size_t)P4_GAME_SIGNAL_MAX_RESULTS;
            CHECK(selected == expected);
            for (size_t index = 0U; index < selected; ++index) {
                const size_t seen_index =
                    (size_t)(window[index].token - 1U);
                CHECK(seen_index < candidate_count);
                if (seen_index < candidate_count) {
                    seen[seen_index] = true;
                }
            }
        }
        for (size_t index = 0U; index < candidate_count; ++index) {
            CHECK(seen[index]);
        }
    }

    size_t next_cursor = 0U;
    const size_t focused = p4_signal_scan_select_window(
        candidates, P4_SIGNAL_SCAN_MAX_CANDIDATES, 0U, 25U,
        window, &next_cursor);
    CHECK(focused == P4_GAME_SIGNAL_MAX_RESULTS);
    CHECK(window[0].token == 25U);
    for (size_t index = 1U; index < focused; ++index) {
        CHECK(window[index].token == (uint64_t)index);
    }
    CHECK(next_cursor == 7U);

    const size_t absent_focus = p4_signal_scan_select_window(
        candidates, 10U, 8U, UINT64_C(999), window, &next_cursor);
    CHECK(absent_focus == P4_GAME_SIGNAL_MAX_RESULTS);
    CHECK(window[0].token == 9U);
    CHECK(window[1].token == 10U);
    CHECK(window[2].token == 1U);
    CHECK(next_cursor == 6U);
    (void)memset(window, UINT8_C(0xa5), sizeof(window));
    CHECK(p4_signal_scan_select_window(
        candidates, P4_SIGNAL_SCAN_MAX_CANDIDATES + 1U, 0U, 0U,
        window, &next_cursor) == 0U);
    CHECK(next_cursor == 0U);
    CHECK(memcmp(window, empty_results, sizeof(window)) == 0);

    if (s_failures != 0) {
        fprintf(stderr, "%d signal scan test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("p4 signal scan tests passed");
    return EXIT_SUCCESS;
}
