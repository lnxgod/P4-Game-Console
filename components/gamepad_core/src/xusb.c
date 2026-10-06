// SPDX-License-Identifier: MIT
#include "gamepad/xusb.h"
#include <limits.h>
#include <string.h>

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8U));
}

static bool complete_interface(bool selected, unsigned declared,
    unsigned seen, const gamepad_xusb_interface_t *candidate)
{
    return !selected || (declared == seen && candidate->endpoint_in != 0U);
}

gamepad_status_t gamepad_xusb_find_interface(const uint8_t *data,
    size_t length, gamepad_xusb_interface_t *result)
{
    if (result == NULL) return GAMEPAD_ERR_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    if (data == NULL) return GAMEPAD_ERR_INVALID_ARGUMENT;
    if (length > GAMEPAD_XUSB_CONFIG_MAX_BYTES)
        return GAMEPAD_ERR_DESCRIPTOR_TOO_LARGE;
    if (length < 9U) return GAMEPAD_ERR_TRUNCATED;
    if (data[0] != 9U || data[1] != 2U || le16(data + 2U) != length ||
        data[4] == 0U || data[4] > 16U)
        return GAMEPAD_ERR_MALFORMED;

    gamepad_xusb_interface_t candidate = {0};
    bool selected = false, found = false;
    unsigned endpoints = 0U, declared = 0U, descriptors = 0U;
    uint32_t endpoint_mask = 0U;
    for (size_t offset = 9U; offset < length;) {
        if (length - offset < 2U) return GAMEPAD_ERR_TRUNCATED;
        const uint8_t *item = data + offset;
        const size_t bytes = item[0];
        if (bytes < 2U) return GAMEPAD_ERR_MALFORMED;
        if (bytes > length - offset) return GAMEPAD_ERR_TRUNCATED;
        if (++descriptors > 128U) return GAMEPAD_ERR_LIMIT_EXCEEDED;
        if (item[1] == 4U) {
            if (bytes != 9U ||
                !complete_interface(selected, declared, endpoints, &candidate))
                return GAMEPAD_ERR_MALFORMED;
            selected = item[3] == 0U && item[5] == 0xFFU &&
                       item[6] == 0x5DU && item[7] == 0x01U;
            if (selected) {
                if (found) return GAMEPAD_ERR_UNSUPPORTED;
                found = true;
                candidate.interface_number = item[2];
                declared = item[4];
                endpoints = 0U;
                endpoint_mask = 0U;
                if (declared == 0U || declared > 2U)
                    return GAMEPAD_ERR_UNSUPPORTED;
            }
        } else if (item[1] == 5U && selected) {
            if (bytes < 7U) return GAMEPAD_ERR_TRUNCATED;
            const uint8_t address = item[2];
            const uint16_t packet = le16(item + 4U);
            const unsigned number = (unsigned)(address & 0x0FU);
            const unsigned bit = number + ((address & 0x80U) != 0U ? 16U : 0U);
            if (number == 0U || (address & 0x70U) != 0U ||
                (endpoint_mask & (UINT32_C(1) << bit)) != 0U ||
                item[3] != 3U || packet == 0U ||
                packet > GAMEPAD_XUSB_PACKET_MAX_BYTES || item[6] == 0U)
                return GAMEPAD_ERR_MALFORMED;
            endpoint_mask |= UINT32_C(1) << bit;
            if (++endpoints > declared) return GAMEPAD_ERR_MALFORMED;
            if ((address & 0x80U) != 0U) {
                if (candidate.endpoint_in != 0U ||
                    packet < GAMEPAD_XUSB_INPUT_BYTES)
                    return GAMEPAD_ERR_UNSUPPORTED;
                candidate.endpoint_in = address;
                candidate.packet_bytes = packet;
            }
        }
        offset += bytes;
    }
    if (!found) return GAMEPAD_ERR_NO_GAMEPAD;
    if (!complete_interface(selected, declared, endpoints, &candidate))
        return GAMEPAD_ERR_MALFORMED;
    *result = candidate;
    return GAMEPAD_OK;
}

static int16_t axis(const uint8_t *bytes, bool invert)
{
    const uint16_t raw = le16(bytes);
    const int32_t signed_value = raw >= 0x8000U ? (int32_t)raw - 65536 : raw;
    const int32_t value = invert ? -signed_value : signed_value;
    return (int16_t)(value > INT16_MAX ? INT16_MAX : value);
}

gamepad_status_t gamepad_xusb_decode(const uint8_t *packet, size_t length,
    uint64_t timestamp_us, gamepad_state_t *state, bool *has_input)
{
    if (has_input == NULL) return GAMEPAD_ERR_INVALID_ARGUMENT;
    *has_input = false;
    if (packet == NULL || state == NULL)
        return GAMEPAD_ERR_INVALID_ARGUMENT;
    if (state->version != GAMEPAD_STATE_VERSION || state->size != sizeof(*state))
        return GAMEPAD_ERR_INVALID_STATE;
    if (state->connected != 1U) return GAMEPAD_ERR_DISCONNECTED;
    if (length > GAMEPAD_XUSB_PACKET_MAX_BYTES)
        return GAMEPAD_ERR_REPORT_TOO_LARGE;
    if (length < 2U || packet[1] != length) return GAMEPAD_ERR_REPORT_SIZE;
    if (packet[0] != 0U) return GAMEPAD_OK;
    if (length != GAMEPAD_XUSB_INPUT_BYTES) return GAMEPAD_ERR_REPORT_SIZE;
    gamepad_state_t next = *state;
    next.buttons = 0U;
    next.dpad = 0U;
    /* Wire bit positions are protocol facts; no HID usages are inferred. */
    static const struct { uint8_t byte, mask, button; } buttons[] = {
        {2,0x10,GAMEPAD_BUTTON_START}, {2,0x20,GAMEPAD_BUTTON_BACK},
        {2,0x40,GAMEPAD_BUTTON_LEFT_STICK}, {2,0x80,GAMEPAD_BUTTON_RIGHT_STICK},
        {3,0x01,GAMEPAD_BUTTON_LEFT_SHOULDER}, {3,0x02,GAMEPAD_BUTTON_RIGHT_SHOULDER},
        {3,0x04,GAMEPAD_BUTTON_GUIDE}, {3,0x10,GAMEPAD_BUTTON_SOUTH},
        {3,0x20,GAMEPAD_BUTTON_EAST}, {3,0x40,GAMEPAD_BUTTON_WEST},
        {3,0x80,GAMEPAD_BUTTON_NORTH},
    };
    for (size_t i = 0U; i < sizeof(buttons)/sizeof(buttons[0]); ++i)
        if ((packet[buttons[i].byte] & buttons[i].mask) != 0U)
            next.buttons |= GAMEPAD_BUTTON_MASK(buttons[i].button);
    if ((packet[2] & 0x03U) == 0x01U) next.dpad |= GAMEPAD_DPAD_UP;
    if ((packet[2] & 0x03U) == 0x02U) next.dpad |= GAMEPAD_DPAD_DOWN;
    if ((packet[2] & 0x0CU) == 0x04U) next.dpad |= GAMEPAD_DPAD_LEFT;
    if ((packet[2] & 0x0CU) == 0x08U) next.dpad |= GAMEPAD_DPAD_RIGHT;
    next.left_trigger = (uint16_t)((uint16_t)packet[4] * 257U);
    next.right_trigger = (uint16_t)((uint16_t)packet[5] * 257U);
    next.left_x = axis(packet + 6U, false);
    next.left_y = axis(packet + 8U, true);
    next.right_x = axis(packet + 10U, false);
    next.right_y = axis(packet + 12U, true);
    next.timestamp_us = timestamp_us;
    next.sequence++;
    *state = next;
    *has_input = true;
    return GAMEPAD_OK;
}
