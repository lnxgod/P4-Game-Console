// SPDX-License-Identifier: MIT

#ifndef P4_SIGNAL_SCAN_H
#define P4_SIGNAL_SCAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/game.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_SIGNAL_SCAN_SSID_MAX_BYTES = 32,
    P4_SIGNAL_SCAN_BSSID_BYTES = 6,
    P4_SIGNAL_SCAN_KEY_BYTES = 16,
};

/**
 * Convert one driver-owned result into the only representation games may see.
 * The keyed token covers BSSID plus SSID and is intentionally session-scoped.
 */
bool p4_signal_scan_make_game_signal(
    const uint8_t key[P4_SIGNAL_SCAN_KEY_BYTES],
    const uint8_t bssid[P4_SIGNAL_SCAN_BSSID_BYTES],
    const uint8_t *ssid,
    size_t ssid_bytes,
    int8_t rssi_dbm,
    uint8_t channel,
    bool protected_network,
    p4_game_signal_t *out_signal);

#ifdef __cplusplus
}
#endif

#endif
