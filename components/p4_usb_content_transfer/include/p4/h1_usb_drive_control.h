// SPDX-License-Identifier: MIT

#ifndef P4_H1_USB_DRIVE_CONTROL_H
#define P4_H1_USB_DRIVE_CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* H1 is a local, physical-admin transport. This protocol is not authentication. */
enum {
    P4_H1_USB_DRIVE_PROTOCOL_VERSION = 1,
    P4_H1_USB_DRIVE_REQUEST_BYTES = 20,
    P4_H1_USB_DRIVE_RESPONSE_BYTES = 28,
    P4_H1_USB_DRIVE_SESSION_TIMEOUT_MS = 10000,
};

typedef enum {
    P4_H1_USB_DRIVE_COMMAND_STATUS = 1,
    P4_H1_USB_DRIVE_COMMAND_SET_MODE = 2,
} p4_h1_usb_drive_command_t;

typedef enum {
    P4_H1_USB_DRIVE_RESULT_OK = 0,
    P4_H1_USB_DRIVE_RESULT_BAD_REQUEST = 1,
    P4_H1_USB_DRIVE_RESULT_UNAVAILABLE = 2,
    P4_H1_USB_DRIVE_RESULT_BUSY = 3,
    P4_H1_USB_DRIVE_RESULT_DENIED = 4,
    P4_H1_USB_DRIVE_RESULT_TRANSITION_FAILED = 5,
    P4_H1_USB_DRIVE_RESULT_SESSION_REQUIRED = 6,
    P4_H1_USB_DRIVE_RESULT_SESSION_EXPIRED = 7,
    P4_H1_USB_DRIVE_RESULT_CRC = 8,
    P4_H1_USB_DRIVE_RESULT_UNSUPPORTED = 9,
    P4_H1_USB_DRIVE_RESULT_STALE_SEQUENCE = 10,
} p4_h1_usb_drive_result_t;

typedef enum {
    P4_H1_USB_DRIVE_TRANSITION_OK = 0,
    P4_H1_USB_DRIVE_TRANSITION_DENIED,
    P4_H1_USB_DRIVE_TRANSITION_FAILED,
} p4_h1_usb_drive_transition_result_t;

/** Snapshot supplied by the application; no display access is involved. */
typedef struct {
    uint8_t storage_state;
    uint32_t storage_generation;
    bool supported;
    bool usb_drive_active;
    bool usb_attached;
    bool usb_host_ejected;
    bool usb_driver_running;
    bool control_available;
} p4_h1_usb_drive_status_t;

typedef bool (*p4_h1_usb_drive_send_fn)(
    void *context, const uint8_t *bytes, size_t bytes_length);
typedef bool (*p4_h1_usb_drive_status_fn)(
    void *context, p4_h1_usb_drive_status_t *out_status);
/**
 * Must perform the full board-owned role transition synchronously. In the
 * Waveshare controller-first image that means stop/neutralize Host+HID before
 * enabling MSC, and disable MSC/remount/rescan before restarting Host+HID.
 */
typedef p4_h1_usb_drive_transition_result_t
    (*p4_h1_usb_drive_set_mode_fn)(void *context, bool enable_usb_drive);

typedef struct {
    uint8_t request[P4_H1_USB_DRIVE_REQUEST_BYTES];
    size_t request_used;
    uint8_t magic_used;
    uint32_t session;
    uint32_t next_sequence;
    uint64_t last_activity_ms;
    p4_h1_usb_drive_send_fn send;
    p4_h1_usb_drive_status_fn status;
    p4_h1_usb_drive_set_mode_fn set_mode;
    void *context;
    bool initialized;
    bool session_active;
} p4_h1_usb_drive_control_t;

bool p4_h1_usb_drive_control_init(
    p4_h1_usb_drive_control_t *control,
    p4_h1_usb_drive_send_fn send,
    p4_h1_usb_drive_status_fn status,
    p4_h1_usb_drive_set_mode_fn set_mode,
    void *context);

/** Feed arbitrary H1 bytes. True only after the P4U1 control magic is seen. */
bool p4_h1_usb_drive_control_consume(
    p4_h1_usb_drive_control_t *control,
    const uint8_t *bytes, size_t bytes_length, uint64_t now_ms);

/** Expire an abandoned control session; safe to call every main-loop tick. */
void p4_h1_usb_drive_control_poll(
    p4_h1_usb_drive_control_t *control, uint64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
