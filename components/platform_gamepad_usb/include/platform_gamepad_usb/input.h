// SPDX-License-Identifier: MIT

#ifndef PLATFORM_GAMEPAD_USB_INPUT_H
#define PLATFORM_GAMEPAD_USB_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_USB_INPUT_SNAPSHOT_VERSION 1U
#define PLATFORM_USB_KEYBOARD_BOOT_KEY_COUNT 6U

/** USB HID usage IDs used by Console OS; values come from Usage Tables 1.5. */
typedef enum {
    PLATFORM_USB_KEY_A = 0x04,
    PLATFORM_USB_KEY_D = 0x07,
    PLATFORM_USB_KEY_R = 0x15,
    PLATFORM_USB_KEY_S = 0x16,
    PLATFORM_USB_KEY_W = 0x1a,
    PLATFORM_USB_KEY_X = 0x1b,
    PLATFORM_USB_KEY_Z = 0x1d,
    PLATFORM_USB_KEY_ENTER = 0x28,
    PLATFORM_USB_KEY_ESCAPE = 0x29,
    PLATFORM_USB_KEY_BACKSPACE = 0x2a,
    PLATFORM_USB_KEY_SPACE = 0x2c,
    PLATFORM_USB_KEY_F5 = 0x3e,
    PLATFORM_USB_KEY_DELETE = 0x4c,
    PLATFORM_USB_KEY_RIGHT = 0x4f,
    PLATFORM_USB_KEY_LEFT = 0x50,
    PLATFORM_USB_KEY_DOWN = 0x51,
    PLATFORM_USB_KEY_UP = 0x52,
} platform_usb_key_usage_t;

typedef enum {
    PLATFORM_USB_INPUT_KIND_NONE = 0,
    PLATFORM_USB_INPUT_KIND_GAMEPAD,
    PLATFORM_USB_INPUT_KIND_KEYBOARD,
    PLATFORM_USB_INPUT_KIND_MOUSE,
} platform_usb_input_kind_t;

typedef struct {
    uint32_t session;
    uint32_t sequence;
    uint64_t timestamp_us;
    uint8_t modifier;
    uint8_t keys[PLATFORM_USB_KEYBOARD_BOOT_KEY_COUNT];
    uint8_t connected;
} platform_usb_keyboard_state_t;

typedef struct {
    uint32_t session;
    uint32_t sequence;
    uint64_t timestamp_us;
    int32_t delta_x;
    int32_t delta_y;
    int32_t wheel;
    uint8_t buttons;
    uint8_t connected;
} platform_usb_mouse_state_t;

typedef struct {
    uint16_t version;
    uint16_t size;
    platform_usb_keyboard_state_t keyboard;
    platform_usb_mouse_state_t mouse;
} platform_usb_input_snapshot_t;

/** True when a complete boot-keyboard snapshot contains the usage code. */
bool platform_usb_keyboard_key_down(
    const platform_usb_keyboard_state_t *keyboard, uint8_t usage);

#ifdef __cplusplus
}
#endif

#endif
