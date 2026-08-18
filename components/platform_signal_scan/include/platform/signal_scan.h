// SPDX-License-Identifier: MIT

#ifndef PLATFORM_SIGNAL_SCAN_H
#define PLATFORM_SIGNAL_SCAN_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "p4/game.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the Console-OS-owned C6 radio service in a background task. */
esp_err_t platform_signal_scan_start(void);

/** True only after the C6 transport and station scanner are ready. */
bool platform_signal_scan_ready(void);

/** Adapter for the P4 Game API. Never blocks on a radio scan. */
bool platform_signal_scan_request(void *context, uint64_t focus_token);

/** Adapter for the P4 Game API. Copies one bounded sanitized snapshot. */
bool platform_signal_scan_read(
    void *context, p4_game_signal_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif
