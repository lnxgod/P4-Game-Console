// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef P4_QUAKE_EMBEDDED_H
#define P4_QUAKE_EMBEDDED_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "platform/touch.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /** Validated, mounted directory which contains the id1 subdirectory. */
    const char *basedir;
    /** Borrowed Console OS touch service; remains OS-owned. */
    platform_touch_t *touch;
    /** Borrowed shared I2C handle used by the OS-owned ES8311 service. */
    void *audio_control_bus;
    uint8_t master_volume_step;
    bool audio_runtime_authorized;
} p4_quake_config_t;

/**
 * Run Quake as a reviewed legacy easter egg.
 *
 * The adapter uses only Console OS display, touch, and audio services. It
 * denies all engine file writes and currently passes `-noudp` until the
 * OS-owned Wi-Fi transport is qualified. Engine global state is not reentrant;
 * Console OS must restart after this call returns.
 */
esp_err_t p4_quake_run(const p4_quake_config_t *config);

#ifdef __cplusplus
}
#endif

#endif
