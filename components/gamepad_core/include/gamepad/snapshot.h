#ifndef GAMEPAD_SNAPSHOT_H
#define GAMEPAD_SNAPSHOT_H

#include <stddef.h>
#include <stdint.h>

#include "gamepad/gamepad.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_GAMEPAD_SNAPSHOT_VERSION 1U
#define PLATFORM_GAMEPAD_DESCRIPTOR_SHA256_BYTES 32U

/** Physical transport that produced a canonical controller snapshot. */
typedef enum {
    PLATFORM_GAMEPAD_TRANSPORT_NONE = 0,
    PLATFORM_GAMEPAD_TRANSPORT_USB_HID = 1,
    PLATFORM_GAMEPAD_TRANSPORT_BLE_HID = 2,
    PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB = 3,
} platform_gamepad_transport_t;

/**
 * Bounded controller identity. BLE HID devices may not expose USB VID/PID, so
 * those fields are zero unless a trusted PnP-ID characteristic was read.
 * descriptor_sha256 hashes the HID report descriptor or XUSB configuration.
 */
typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t interface_number;
    uint8_t transport;
    uint8_t reserved[2];
    uint8_t descriptor_sha256[PLATFORM_GAMEPAD_DESCRIPTOR_SHA256_BYTES];
} platform_gamepad_identity_t;

/** One complete game-facing publication unit. */
typedef struct {
    uint16_t version;
    uint16_t size;
    uint32_t session;
    uint32_t capabilities;
    gamepad_state_t state;
    platform_gamepad_identity_t identity;
} platform_gamepad_snapshot_t;

/** Portable transactional model; the owning transport supplies locking. */
typedef struct {
    platform_gamepad_snapshot_t snapshot;
    uint8_t initialized;
    uint8_t reserved[3];
} platform_gamepad_model_t;

void platform_gamepad_model_init(platform_gamepad_model_t *model);
gamepad_status_t platform_gamepad_model_copy(
    const platform_gamepad_model_t *model,
    platform_gamepad_snapshot_t *snapshot);
gamepad_status_t platform_gamepad_model_connect(
    platform_gamepad_model_t *model,
    const platform_gamepad_identity_t *identity,
    uint32_t capabilities,
    uint64_t timestamp_us,
    uint32_t *session);
gamepad_status_t platform_gamepad_model_disconnect(
    platform_gamepad_model_t *model,
    uint32_t expected_session,
    uint64_t timestamp_us);
gamepad_status_t platform_gamepad_model_commit_report(
    platform_gamepad_model_t *model,
    uint32_t expected_session,
    const gamepad_state_t *decoded_state);

#ifdef __cplusplus
}
#endif

#endif
