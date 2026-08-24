// SPDX-License-Identifier: MIT

#ifndef PLATFORM_RADIO_HOSTED_H
#define PLATFORM_RADIO_HOSTED_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLATFORM_RADIO_HOSTED_OFF = 0,
    PLATFORM_RADIO_HOSTED_STARTING,
    PLATFORM_RADIO_HOSTED_READY,
    PLATFORM_RADIO_HOSTED_FAILED,
} platform_radio_hosted_state_t;

typedef struct {
    platform_radio_hosted_state_t state;
    esp_err_t last_error;
    uint32_t firmware_major;
    uint32_t firmware_minor;
    uint32_t firmware_patch;
    bool firmware_version_known;
} platform_radio_hosted_status_t;

/**
 * Start the board-authorized P4-to-C6 ESP-Hosted transport exactly once.
 * This call can block while the SDIO link and coprocessor initialize, so an
 * interactive service should invoke it from its own worker task.
 */
esp_err_t platform_radio_hosted_start(void);

platform_radio_hosted_status_t platform_radio_hosted_status(void);

#ifdef __cplusplus
}
#endif

#endif
