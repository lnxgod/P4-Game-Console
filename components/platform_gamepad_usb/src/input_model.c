// SPDX-License-Identifier: MIT

#include "platform_gamepad_usb/input_model.h"

#include <limits.h>
#include <string.h>

enum {
    BOOT_KEYBOARD_REPORT_BYTES = 8,
    BOOT_MOUSE_MIN_REPORT_BYTES = 3,
    BOOT_MOUSE_MAX_REPORT_BYTES = 4,
    BOOT_MOUSE_BUTTON_MASK = 0x07,
    HID_KEY_ERROR_UNDEFINED = 3,
};

static bool model_valid(const platform_usb_input_model_t *model)
{
    return model != NULL && model->initialized == 1U &&
        model->snapshot.version == PLATFORM_USB_INPUT_SNAPSHOT_VERSION &&
        model->snapshot.size == sizeof(model->snapshot) &&
        model->snapshot.keyboard.connected <= 1U &&
        model->snapshot.mouse.connected <= 1U;
}

static uint32_t next_nonzero(uint32_t value)
{
    ++value;
    return value == 0U ? 1U : value;
}

static int32_t saturating_add(int32_t left, int32_t right)
{
    if (right > 0 && left > INT32_MAX - right) {
        return INT32_MAX;
    }
    if (right < 0 && left < INT32_MIN - right) {
        return INT32_MIN;
    }
    return left + right;
}

bool platform_usb_keyboard_key_down(
    const platform_usb_keyboard_state_t *keyboard, uint8_t usage)
{
    if (keyboard == NULL || keyboard->connected == 0U ||
        usage <= HID_KEY_ERROR_UNDEFINED) {
        return false;
    }
    for (size_t index = 0U;
         index < PLATFORM_USB_KEYBOARD_BOOT_KEY_COUNT; ++index) {
        if (keyboard->keys[index] == usage) {
            return true;
        }
    }
    return false;
}

void platform_usb_input_model_init(platform_usb_input_model_t *model)
{
    if (model == NULL) {
        return;
    }
    memset(model, 0, sizeof(*model));
    model->initialized = 1U;
    model->snapshot.version = PLATFORM_USB_INPUT_SNAPSHOT_VERSION;
    model->snapshot.size = (uint16_t)sizeof(model->snapshot);
}

platform_usb_input_status_t platform_usb_input_model_connect(
    platform_usb_input_model_t *model,
    platform_usb_input_kind_t kind,
    uint64_t timestamp_us,
    uint32_t *session)
{
    if (!model_valid(model) || session == NULL ||
        (kind != PLATFORM_USB_INPUT_KIND_KEYBOARD &&
         kind != PLATFORM_USB_INPUT_KIND_MOUSE)) {
        return PLATFORM_USB_INPUT_ERR_INVALID_ARGUMENT;
    }
    if (kind == PLATFORM_USB_INPUT_KIND_KEYBOARD) {
        platform_usb_keyboard_state_t *const keyboard =
            &model->snapshot.keyboard;
        if (keyboard->connected != 0U) {
            return PLATFORM_USB_INPUT_ERR_INVALID_STATE;
        }
        const uint32_t sequence = keyboard->sequence;
        const uint32_t next_session = next_nonzero(keyboard->session);
        memset(keyboard, 0, sizeof(*keyboard));
        keyboard->session = next_session;
        keyboard->sequence = next_nonzero(sequence);
        keyboard->timestamp_us = timestamp_us;
        keyboard->connected = 1U;
        *session = next_session;
        return PLATFORM_USB_INPUT_OK;
    }

    platform_usb_mouse_state_t *const mouse = &model->snapshot.mouse;
    if (mouse->connected != 0U) {
        return PLATFORM_USB_INPUT_ERR_INVALID_STATE;
    }
    const uint32_t sequence = mouse->sequence;
    const uint32_t next_session = next_nonzero(mouse->session);
    memset(mouse, 0, sizeof(*mouse));
    mouse->session = next_session;
    mouse->sequence = next_nonzero(sequence);
    mouse->timestamp_us = timestamp_us;
    mouse->connected = 1U;
    *session = next_session;
    return PLATFORM_USB_INPUT_OK;
}

platform_usb_input_status_t platform_usb_input_model_disconnect(
    platform_usb_input_model_t *model,
    platform_usb_input_kind_t kind,
    uint32_t expected_session,
    uint64_t timestamp_us)
{
    if (!model_valid(model) || expected_session == 0U) {
        return PLATFORM_USB_INPUT_ERR_INVALID_ARGUMENT;
    }
    if (kind == PLATFORM_USB_INPUT_KIND_KEYBOARD) {
        platform_usb_keyboard_state_t *const keyboard =
            &model->snapshot.keyboard;
        if (keyboard->connected == 0U ||
            keyboard->session != expected_session) {
            return PLATFORM_USB_INPUT_ERR_DISCONNECTED;
        }
        memset(keyboard->keys, 0, sizeof(keyboard->keys));
        keyboard->modifier = 0U;
        keyboard->connected = 0U;
        keyboard->sequence = next_nonzero(keyboard->sequence);
        keyboard->timestamp_us = timestamp_us;
        return PLATFORM_USB_INPUT_OK;
    }
    if (kind == PLATFORM_USB_INPUT_KIND_MOUSE) {
        platform_usb_mouse_state_t *const mouse = &model->snapshot.mouse;
        if (mouse->connected == 0U || mouse->session != expected_session) {
            return PLATFORM_USB_INPUT_ERR_DISCONNECTED;
        }
        mouse->delta_x = 0;
        mouse->delta_y = 0;
        mouse->wheel = 0;
        mouse->buttons = 0U;
        mouse->connected = 0U;
        mouse->sequence = next_nonzero(mouse->sequence);
        mouse->timestamp_us = timestamp_us;
        return PLATFORM_USB_INPUT_OK;
    }
    return PLATFORM_USB_INPUT_ERR_INVALID_ARGUMENT;
}

static bool keyboard_report_valid(const uint8_t *report)
{
    for (size_t index = 0U;
         index < PLATFORM_USB_KEYBOARD_BOOT_KEY_COUNT; ++index) {
        const uint8_t key = report[index + 2U];
        if (key != 0U && key <= HID_KEY_ERROR_UNDEFINED) {
            return false;
        }
        if (key == 0U) {
            continue;
        }
        for (size_t prior = 0U; prior < index; ++prior) {
            if (report[prior + 2U] == key) {
                return false;
            }
        }
    }
    return true;
}

platform_usb_input_status_t platform_usb_input_model_commit_boot_report(
    platform_usb_input_model_t *model,
    platform_usb_input_kind_t kind,
    uint32_t expected_session,
    const uint8_t *report,
    size_t report_bytes,
    uint64_t timestamp_us)
{
    if (!model_valid(model) || expected_session == 0U || report == NULL) {
        return PLATFORM_USB_INPUT_ERR_INVALID_ARGUMENT;
    }
    if (kind == PLATFORM_USB_INPUT_KIND_KEYBOARD) {
        platform_usb_keyboard_state_t *const keyboard =
            &model->snapshot.keyboard;
        if (keyboard->connected == 0U ||
            keyboard->session != expected_session) {
            return PLATFORM_USB_INPUT_ERR_DISCONNECTED;
        }
        if (report_bytes != BOOT_KEYBOARD_REPORT_BYTES || report[1] != 0U ||
            !keyboard_report_valid(report)) {
            return PLATFORM_USB_INPUT_ERR_MALFORMED_REPORT;
        }
        keyboard->modifier = report[0];
        memcpy(keyboard->keys, &report[2], sizeof(keyboard->keys));
        keyboard->sequence = next_nonzero(keyboard->sequence);
        keyboard->timestamp_us = timestamp_us;
        return PLATFORM_USB_INPUT_OK;
    }
    if (kind == PLATFORM_USB_INPUT_KIND_MOUSE) {
        platform_usb_mouse_state_t *const mouse = &model->snapshot.mouse;
        if (mouse->connected == 0U || mouse->session != expected_session) {
            return PLATFORM_USB_INPUT_ERR_DISCONNECTED;
        }
        if (report_bytes < BOOT_MOUSE_MIN_REPORT_BYTES ||
            report_bytes > BOOT_MOUSE_MAX_REPORT_BYTES ||
            (report[0] & (uint8_t)~BOOT_MOUSE_BUTTON_MASK) != 0U) {
            return PLATFORM_USB_INPUT_ERR_MALFORMED_REPORT;
        }
        mouse->buttons = report[0];
        mouse->delta_x = saturating_add(
            mouse->delta_x, (int32_t)(int8_t)report[1]);
        mouse->delta_y = saturating_add(
            mouse->delta_y, (int32_t)(int8_t)report[2]);
        if (report_bytes == BOOT_MOUSE_MAX_REPORT_BYTES) {
            mouse->wheel = saturating_add(
                mouse->wheel, (int32_t)(int8_t)report[3]);
        }
        mouse->sequence = next_nonzero(mouse->sequence);
        mouse->timestamp_us = timestamp_us;
        return PLATFORM_USB_INPUT_OK;
    }
    return PLATFORM_USB_INPUT_ERR_INVALID_ARGUMENT;
}

platform_usb_input_status_t platform_usb_input_model_take(
    platform_usb_input_model_t *model,
    platform_usb_input_snapshot_t *snapshot)
{
    if (!model_valid(model) || snapshot == NULL) {
        return PLATFORM_USB_INPUT_ERR_INVALID_ARGUMENT;
    }
    *snapshot = model->snapshot;
    model->snapshot.mouse.delta_x = 0;
    model->snapshot.mouse.delta_y = 0;
    model->snapshot.mouse.wheel = 0;
    return PLATFORM_USB_INPUT_OK;
}
