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
    P4_SIGNAL_SCAN_MAX_CANDIDATES = 32,
};

/**
 * Convert one driver-owned result into the only representation games may see.
 * The keyed token covers BSSID plus SSID. Key lifetime is owned by Console OS;
 * games never receive either the key or a raw hardware address.
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

/**
 * Remove every published result while preserving snapshot status/generation.
 * Providers must call this whenever publishing a non-READY status so the
 * Game API never observes stale identities attached to SCANNING or an error.
 */
void p4_signal_scan_clear_results(p4_game_signal_snapshot_t *snapshot);

/**
 * Select one bounded, deterministic window from an already ordered candidate
 * set. A present nonzero focus token is emitted first; the remaining slots
 * walk from window_cursor and wrap once. next_window_cursor continues after
 * the last inspected candidate so repeated general scans expose all 32.
 */
size_t p4_signal_scan_select_window(
    const p4_game_signal_t *candidates,
    size_t candidate_count,
    size_t window_cursor,
    uint64_t focus_token,
    p4_game_signal_t *out_results,
    size_t *next_window_cursor);

#ifdef __cplusplus
}
#endif

#endif
