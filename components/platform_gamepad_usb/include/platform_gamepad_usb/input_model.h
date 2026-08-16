// SPDX-License-Identifier: MIT

#ifndef PLATFORM_GAMEPAD_USB_INPUT_MODEL_H
#define PLATFORM_GAMEPAD_USB_INPUT_MODEL_H

#include <stddef.h>

#include "platform_gamepad_usb/input.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLATFORM_USB_INPUT_OK = 0,
    PLATFORM_USB_INPUT_ERR_INVALID_ARGUMENT = -1,
    PLATFORM_USB_INPUT_ERR_INVALID_STATE = -2,
    PLATFORM_USB_INPUT_ERR_DISCONNECTED = -3,
    PLATFORM_USB_INPUT_ERR_MALFORMED_REPORT = -4,
} platform_usb_input_status_t;

typedef struct {
    platform_usb_input_snapshot_t snapshot;
    uint8_t initialized;
} platform_usb_input_model_t;

void platform_usb_input_model_init(platform_usb_input_model_t *model);

platform_usb_input_status_t platform_usb_input_model_connect(
    platform_usb_input_model_t *model,
    platform_usb_input_kind_t kind,
    uint64_t timestamp_us,
    uint32_t *session);

platform_usb_input_status_t platform_usb_input_model_disconnect(
    platform_usb_input_model_t *model,
    platform_usb_input_kind_t kind,
    uint32_t expected_session,
    uint64_t timestamp_us);

platform_usb_input_status_t platform_usb_input_model_commit_boot_report(
    platform_usb_input_model_t *model,
    platform_usb_input_kind_t kind,
    uint32_t expected_session,
    const uint8_t *report,
    size_t report_bytes,
    uint64_t timestamp_us);

/**
 * Copy one coherent keyboard/mouse snapshot and consume relative mouse motion.
 * Held keys and mouse buttons remain published until a later report or detach.
 */
platform_usb_input_status_t platform_usb_input_model_take(
    platform_usb_input_model_t *model,
    platform_usb_input_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif
