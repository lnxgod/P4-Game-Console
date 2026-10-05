// SPDX-License-Identifier: MIT
#ifndef PLATFORM_DICE_BLE_H
#define PLATFORM_DICE_BLE_H
#include "esp_err.h"
#include "p4/dice.h"
/* Register before the shared host starts; does not turn on the radio. */
esp_err_t platform_dice_ble_prepare(void);
bool platform_dice_ble_exchange(void *context, const p4_dice_request_t *request,
    p4_dice_status_t *status);
void platform_dice_ble_close(void);
#endif
