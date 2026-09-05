// SPDX-License-Identifier: MIT

#include "p4/h1_usb_drive_control.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t response[P4_H1_USB_DRIVE_RESPONSE_BYTES];
    size_t response_bytes;
    p4_h1_usb_drive_status_t status;
    p4_h1_usb_drive_transition_result_t transition;
    unsigned calls;
    bool requested_mode;
} fixture_t;

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

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool send(void *context, const uint8_t *bytes, size_t length)
{
    fixture_t *const fixture = context;
    assert(length == sizeof(fixture->response));
    memcpy(fixture->response, bytes, length);
    fixture->response_bytes = length;
    return true;
}

static bool status(void *context, p4_h1_usb_drive_status_t *out_status)
{
    const fixture_t *const fixture = context;
    *out_status = fixture->status;
    return true;
}

static p4_h1_usb_drive_transition_result_t set_mode(
    void *context, bool enable_usb_drive)
{
    fixture_t *const fixture = context;
    ++fixture->calls;
    fixture->requested_mode = enable_usb_drive;
    fixture->status.usb_drive_active = enable_usb_drive;
    return fixture->transition;
}

static void make_request(uint8_t request[P4_H1_USB_DRIVE_REQUEST_BYTES],
                         uint8_t command, uint8_t mode,
                         uint32_t session, uint32_t sequence)
{
    memset(request, 0, P4_H1_USB_DRIVE_REQUEST_BYTES);
    memcpy(request, "P4U1", 4U);
    request[4] = P4_H1_USB_DRIVE_PROTOCOL_VERSION;
    request[5] = command;
    request[6] = mode;
    write_u32_le(&request[8], session);
    write_u32_le(&request[12], sequence);
    write_u32_le(&request[16], crc32_bytes(request, 16U));
}

static void expect_result(const fixture_t *fixture, uint8_t command,
                          p4_h1_usb_drive_result_t result)
{
    assert(fixture->response_bytes == P4_H1_USB_DRIVE_RESPONSE_BYTES);
    assert(memcmp(fixture->response, "P4V1", 4U) == 0);
    assert(fixture->response[4] == P4_H1_USB_DRIVE_PROTOCOL_VERSION);
    assert(fixture->response[5] == command);
    assert(fixture->response[6] == result);
    assert(read_u32_le(&fixture->response[24]) ==
           crc32_bytes(fixture->response, 24U));
}

static p4_h1_usb_drive_control_t initialized(fixture_t *fixture)
{
    p4_h1_usb_drive_control_t control;
    memset(fixture, 0, sizeof(*fixture));
    fixture->status.supported = true;
    fixture->status.control_available = true;
    fixture->status.storage_state = 2U;
    fixture->status.storage_generation = 7U;
    fixture->transition = P4_H1_USB_DRIVE_TRANSITION_OK;
    assert(p4_h1_usb_drive_control_init(&control, send, status, set_mode,
                                        fixture));
    return control;
}

static void test_session_and_transition(void)
{
    fixture_t fixture;
    p4_h1_usb_drive_control_t control = initialized(&fixture);
    uint8_t request[P4_H1_USB_DRIVE_REQUEST_BYTES];
    make_request(request, P4_H1_USB_DRIVE_COMMAND_STATUS, 0U, 0x1234U, 0U);
    assert(p4_h1_usb_drive_control_consume(&control, request, 3U, 1U) == false);
    assert(p4_h1_usb_drive_control_consume(&control, request + 3U,
                                            sizeof(request) - 3U, 1U));
    expect_result(&fixture, P4_H1_USB_DRIVE_COMMAND_STATUS,
                  P4_H1_USB_DRIVE_RESULT_OK);

    make_request(request, P4_H1_USB_DRIVE_COMMAND_SET_MODE, 1U, 0x1234U, 1U);
    assert(p4_h1_usb_drive_control_consume(&control, request, sizeof(request), 2U));
    expect_result(&fixture, P4_H1_USB_DRIVE_COMMAND_SET_MODE,
                  P4_H1_USB_DRIVE_RESULT_OK);
    assert(fixture.calls == 1U && fixture.requested_mode);
    assert(fixture.response[7] == 1U);

    assert(p4_h1_usb_drive_control_consume(&control, request, sizeof(request), 3U));
    expect_result(&fixture, P4_H1_USB_DRIVE_COMMAND_SET_MODE,
                  P4_H1_USB_DRIVE_RESULT_STALE_SEQUENCE);
    assert(fixture.calls == 1U);
}

static void test_crc_and_eject_denial(void)
{
    fixture_t fixture;
    p4_h1_usb_drive_control_t control = initialized(&fixture);
    uint8_t request[P4_H1_USB_DRIVE_REQUEST_BYTES];
    make_request(request, P4_H1_USB_DRIVE_COMMAND_STATUS, 0U, 5U, 0U);
    request[19] ^= UINT8_C(0x80);
    assert(p4_h1_usb_drive_control_consume(&control, request, sizeof(request), 1U));
    expect_result(&fixture, P4_H1_USB_DRIVE_COMMAND_STATUS,
                  P4_H1_USB_DRIVE_RESULT_CRC);
    assert(fixture.calls == 0U);

    make_request(request, P4_H1_USB_DRIVE_COMMAND_STATUS, 0U, 5U, 0U);
    assert(p4_h1_usb_drive_control_consume(&control, request, sizeof(request), 2U));
    fixture.transition = P4_H1_USB_DRIVE_TRANSITION_DENIED;
    fixture.status.usb_attached = true;
    fixture.status.usb_host_ejected = false;
    make_request(request, P4_H1_USB_DRIVE_COMMAND_SET_MODE, 0U, 5U, 1U);
    assert(p4_h1_usb_drive_control_consume(&control, request, sizeof(request), 3U));
    expect_result(&fixture, P4_H1_USB_DRIVE_COMMAND_SET_MODE,
                  P4_H1_USB_DRIVE_RESULT_DENIED);
    assert(fixture.calls == 1U && !fixture.requested_mode);
}

static void test_expired_session_cannot_switch_mode(void)
{
    fixture_t fixture;
    p4_h1_usb_drive_control_t control = initialized(&fixture);
    uint8_t request[P4_H1_USB_DRIVE_REQUEST_BYTES];
    make_request(request, P4_H1_USB_DRIVE_COMMAND_STATUS, 0U, 6U, 0U);
    assert(p4_h1_usb_drive_control_consume(&control, request, sizeof(request), 10U));
    make_request(request, P4_H1_USB_DRIVE_COMMAND_SET_MODE, 1U, 6U, 1U);
    assert(p4_h1_usb_drive_control_consume(
        &control, request, sizeof(request),
        10U + P4_H1_USB_DRIVE_SESSION_TIMEOUT_MS + 1U));
    expect_result(&fixture, P4_H1_USB_DRIVE_COMMAND_SET_MODE,
                  P4_H1_USB_DRIVE_RESULT_SESSION_EXPIRED);
    assert(fixture.calls == 0U);
}

int main(void)
{
    test_session_and_transition();
    test_crc_and_eject_denial();
    test_expired_session_cannot_switch_mode();
    puts("h1 usb drive control tests passed");
    return 0;
}
