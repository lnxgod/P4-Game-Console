// SPDX-License-Identifier: MIT

#include "p4/h1_usb_drive_control.h"

#include <string.h>

enum {
    REQUEST_CRC_OFFSET = 16,
    RESPONSE_CRC_OFFSET = 24,
    REQUEST_RESERVED_OFFSET = 7,
    REQUEST_SESSION_OFFSET = 8,
    REQUEST_SEQUENCE_OFFSET = 12,
    RESPONSE_VERSION_OFFSET = 4,
    RESPONSE_COMMAND_OFFSET = 5,
    RESPONSE_RESULT_OFFSET = 6,
    RESPONSE_MODE_OFFSET = 7,
    RESPONSE_STORAGE_STATE_OFFSET = 8,
    RESPONSE_FLAGS_OFFSET = 9,
    RESPONSE_GENERATION_OFFSET = 12,
    RESPONSE_SESSION_OFFSET = 16,
    RESPONSE_SEQUENCE_OFFSET = 20,
};

static const uint8_t REQUEST_MAGIC[4] = {'P', '4', 'U', '1'};
static const uint8_t RESPONSE_MAGIC[4] = {'P', '4', 'V', '1'};

static uint32_t read_u32_le(const uint8_t bytes[4])
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static void write_u32_le(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static uint32_t crc32_bytes(const uint8_t *bytes, size_t length)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

static void reset_parser(p4_h1_usb_drive_control_t *control)
{
    control->request_used = 0U;
    control->magic_used = 0U;
}

static bool expire_session(p4_h1_usb_drive_control_t *control,
                           uint64_t now_ms)
{
    if (control->session_active && now_ms >= control->last_activity_ms &&
        now_ms - control->last_activity_ms >
            P4_H1_USB_DRIVE_SESSION_TIMEOUT_MS) {
        control->session_active = false;
        control->session = 0U;
        control->next_sequence = 0U;
        return true;
    }
    return false;
}

void p4_h1_usb_drive_control_poll(
    p4_h1_usb_drive_control_t *control, uint64_t now_ms)
{
    if (control != NULL && control->initialized) {
        (void)expire_session(control, now_ms);
    }
}

static void send_response(p4_h1_usb_drive_control_t *control,
                          p4_h1_usb_drive_command_t command,
                          p4_h1_usb_drive_result_t result,
                          uint32_t session, uint32_t sequence)
{
    if (control->send == NULL || control->status == NULL) {
        return;
    }
    p4_h1_usb_drive_status_t status;
    memset(&status, 0, sizeof(status));
    const bool status_ok = control->status(control->context, &status);
    uint8_t response[P4_H1_USB_DRIVE_RESPONSE_BYTES] = {0};
    memcpy(response, RESPONSE_MAGIC, sizeof(RESPONSE_MAGIC));
    response[RESPONSE_VERSION_OFFSET] = P4_H1_USB_DRIVE_PROTOCOL_VERSION;
    response[RESPONSE_COMMAND_OFFSET] = (uint8_t)command;
    response[RESPONSE_RESULT_OFFSET] = status_ok
        ? (uint8_t)result : (uint8_t)P4_H1_USB_DRIVE_RESULT_UNAVAILABLE;
    response[RESPONSE_MODE_OFFSET] = status.usb_drive_active ? 1U : 0U;
    response[RESPONSE_STORAGE_STATE_OFFSET] = status.storage_state;
    response[RESPONSE_FLAGS_OFFSET] =
        (status.supported ? UINT8_C(1) : UINT8_C(0)) |
        (status.usb_attached ? UINT8_C(2) : UINT8_C(0)) |
        (status.usb_host_ejected ? UINT8_C(4) : UINT8_C(0)) |
        (status.usb_driver_running ? UINT8_C(8) : UINT8_C(0)) |
        (status.control_available ? UINT8_C(16) : UINT8_C(0));
    write_u32_le(&response[RESPONSE_GENERATION_OFFSET],
                 status.storage_generation);
    write_u32_le(&response[RESPONSE_SESSION_OFFSET], session);
    write_u32_le(&response[RESPONSE_SEQUENCE_OFFSET], sequence);
    write_u32_le(&response[RESPONSE_CRC_OFFSET],
                 crc32_bytes(response, RESPONSE_CRC_OFFSET));
    (void)control->send(control->context, response, sizeof(response));
}

static p4_h1_usb_drive_result_t validate_request(
    const p4_h1_usb_drive_control_t *control,
    p4_h1_usb_drive_command_t *out_command, bool *out_mode,
    uint32_t *out_session, uint32_t *out_sequence)
{
    const uint8_t *const request = control->request;
    if (memcmp(request, REQUEST_MAGIC, sizeof(REQUEST_MAGIC)) != 0 ||
        read_u32_le(&request[REQUEST_CRC_OFFSET]) !=
            crc32_bytes(request, REQUEST_CRC_OFFSET)) {
        return P4_H1_USB_DRIVE_RESULT_CRC;
    }
    if (request[4] != P4_H1_USB_DRIVE_PROTOCOL_VERSION ||
        request[REQUEST_RESERVED_OFFSET] != 0U) {
        return P4_H1_USB_DRIVE_RESULT_BAD_REQUEST;
    }
    const uint8_t command = request[5];
    const uint8_t mode = request[6];
    if ((command != P4_H1_USB_DRIVE_COMMAND_STATUS &&
         command != P4_H1_USB_DRIVE_COMMAND_SET_MODE) ||
        (command == P4_H1_USB_DRIVE_COMMAND_STATUS && mode != 0U) ||
        (command == P4_H1_USB_DRIVE_COMMAND_SET_MODE && mode > 1U)) {
        return P4_H1_USB_DRIVE_RESULT_BAD_REQUEST;
    }
    const uint32_t session = read_u32_le(&request[REQUEST_SESSION_OFFSET]);
    if (session == 0U) {
        return P4_H1_USB_DRIVE_RESULT_BAD_REQUEST;
    }
    *out_command = (p4_h1_usb_drive_command_t)command;
    *out_mode = mode != 0U;
    *out_session = session;
    *out_sequence = read_u32_le(&request[REQUEST_SEQUENCE_OFFSET]);
    return P4_H1_USB_DRIVE_RESULT_OK;
}

static void handle_request(p4_h1_usb_drive_control_t *control,
                           uint64_t now_ms)
{
    p4_h1_usb_drive_command_t command = P4_H1_USB_DRIVE_COMMAND_STATUS;
    bool mode = false;
    uint32_t session = 0U;
    uint32_t sequence = 0U;
    p4_h1_usb_drive_result_t result = validate_request(
        control, &command, &mode, &session, &sequence);
    if (result != P4_H1_USB_DRIVE_RESULT_OK) {
        send_response(control, command, result, session, sequence);
        return;
    }
    const bool session_expired = expire_session(control, now_ms);
    if (command == P4_H1_USB_DRIVE_COMMAND_STATUS) {
        if (sequence != 0U) {
            result = P4_H1_USB_DRIVE_RESULT_BAD_REQUEST;
        } else if (control->session_active && control->session != session) {
            result = P4_H1_USB_DRIVE_RESULT_BUSY;
        } else {
            control->session_active = true;
            control->session = session;
            control->next_sequence = 1U;
            control->last_activity_ms = now_ms;
        }
        send_response(control, command, result, session, sequence);
        return;
    }
    if (!control->session_active || control->session != session) {
        send_response(control, command,
                      session_expired
                          ? P4_H1_USB_DRIVE_RESULT_SESSION_EXPIRED
                          : P4_H1_USB_DRIVE_RESULT_SESSION_REQUIRED,
                      session, sequence);
        return;
    }
    if (sequence != control->next_sequence) {
        send_response(control, command, P4_H1_USB_DRIVE_RESULT_STALE_SEQUENCE,
                      session, sequence);
        return;
    }
    p4_h1_usb_drive_status_t status;
    memset(&status, 0, sizeof(status));
    if (control->status == NULL || !control->status(control->context, &status) ||
        !status.supported) {
        result = P4_H1_USB_DRIVE_RESULT_UNSUPPORTED;
    } else if (!status.control_available) {
        result = P4_H1_USB_DRIVE_RESULT_UNAVAILABLE;
    } else if (control->set_mode == NULL) {
        result = P4_H1_USB_DRIVE_RESULT_UNSUPPORTED;
    } else {
        const p4_h1_usb_drive_transition_result_t transition =
            control->set_mode(control->context, mode);
        result = transition == P4_H1_USB_DRIVE_TRANSITION_OK
            ? P4_H1_USB_DRIVE_RESULT_OK
            : transition == P4_H1_USB_DRIVE_TRANSITION_DENIED
                ? P4_H1_USB_DRIVE_RESULT_DENIED
                : P4_H1_USB_DRIVE_RESULT_TRANSITION_FAILED;
    }
    control->last_activity_ms = now_ms;
    if (control->next_sequence != UINT32_MAX) {
        ++control->next_sequence;
    }
    send_response(control, command, result, session, sequence);
}

bool p4_h1_usb_drive_control_init(
    p4_h1_usb_drive_control_t *control,
    p4_h1_usb_drive_send_fn send,
    p4_h1_usb_drive_status_fn status,
    p4_h1_usb_drive_set_mode_fn set_mode,
    void *context)
{
    if (control == NULL || send == NULL || status == NULL || set_mode == NULL) {
        return false;
    }
    memset(control, 0, sizeof(*control));
    control->send = send;
    control->status = status;
    control->set_mode = set_mode;
    control->context = context;
    control->initialized = true;
    return true;
}

bool p4_h1_usb_drive_control_consume(
    p4_h1_usb_drive_control_t *control,
    const uint8_t *bytes, size_t bytes_length, uint64_t now_ms)
{
    if (control == NULL || !control->initialized ||
        (bytes == NULL && bytes_length != 0U)) {
        return false;
    }
    bool claimed = control->request_used != 0U;
    for (size_t index = 0U; index < bytes_length; ++index) {
        const uint8_t byte = bytes[index];
        if (control->request_used != 0U) {
            control->request[control->request_used++] = byte;
            if (control->request_used == P4_H1_USB_DRIVE_REQUEST_BYTES) {
                handle_request(control, now_ms);
                reset_parser(control);
            }
            continue;
        }
        if (byte == REQUEST_MAGIC[control->magic_used]) {
            ++control->magic_used;
            if (control->magic_used == sizeof(REQUEST_MAGIC)) {
                memcpy(control->request, REQUEST_MAGIC, sizeof(REQUEST_MAGIC));
                control->request_used = sizeof(REQUEST_MAGIC);
                control->magic_used = 0U;
                claimed = true;
            }
        } else {
            control->magic_used = byte == REQUEST_MAGIC[0] ? 1U : 0U;
        }
    }
    return claimed;
}
