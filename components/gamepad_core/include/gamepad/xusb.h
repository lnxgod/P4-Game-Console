// SPDX-License-Identifier: MIT
#ifndef GAMEPAD_XUSB_H
#define GAMEPAD_XUSB_H
#include "gamepad/gamepad.h"

#define GAMEPAD_XUSB_CONFIG_MAX_BYTES 4096U
#define GAMEPAD_XUSB_PACKET_MAX_BYTES 64U
#define GAMEPAD_XUSB_INPUT_BYTES 20U
#define GAMEPAD_XUSB_CAPABILITIES (GAMEPAD_CAP_BUTTONS | GAMEPAD_CAP_DPAD | \
    GAMEPAD_CAP_LEFT_STICK | GAMEPAD_CAP_RIGHT_STICK | \
    GAMEPAD_CAP_LEFT_TRIGGER | GAMEPAD_CAP_RIGHT_TRIGGER)

typedef struct {
    uint8_t interface_number;
    uint8_t endpoint_in;
    uint16_t packet_bytes;
} gamepad_xusb_interface_t;

/* Select one alternate-zero wired XUSB interface (ff/5d/01). This does not
 * recognize GIP (ff/47/d0), wireless receivers (ff/5d/81), HID or device quirks.
 * Every descriptor length is checked before use; output is empty on failure. */
gamepad_status_t gamepad_xusb_find_interface(const uint8_t *configuration,
    size_t length, gamepad_xusb_interface_t *interface_out);

/* Decode a wired 20-byte XUSB input packet into a connected snapshot.
 * Bounded, correctly framed non-input packets return OK with has_input=false.
 * Malformed input is transactional; transport must neutralize on fatal errors.
 * Y is normalized to negative=up, preserving a zero center. */
gamepad_status_t gamepad_xusb_decode(const uint8_t *packet, size_t length,
    uint64_t timestamp_us, gamepad_state_t *state, bool *has_input);
#endif
