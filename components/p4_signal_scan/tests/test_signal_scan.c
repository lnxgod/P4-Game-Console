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

    if (s_failures != 0) {
        fprintf(stderr, "%d signal scan test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("p4 signal scan tests passed");
    return EXIT_SUCCESS;
}
