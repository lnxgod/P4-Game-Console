// SPDX-License-Identifier: MIT
#ifndef PLATFORM_GAMEPAD_XUSB_H
#define PLATFORM_GAMEPAD_XUSB_H
#include "platform/gamepad.h"
#include "freertos/FreeRTOS.h"

/* Wired Xbox 360-format XUSB only; no wireless receiver or Xbox One/Series GIP.
 * Install after the shared native USB host starts, before enabling its root. */
esp_err_t platform_gamepad_xusb_start(void);
/* Shared host must be QUIESCING first. Uncertain USB resources are retained. */
esp_err_t platform_gamepad_xusb_stop(TickType_t timeout_ticks);
esp_err_t platform_gamepad_xusb_get_snapshot(platform_gamepad_snapshot_t *snapshot);
#endif
