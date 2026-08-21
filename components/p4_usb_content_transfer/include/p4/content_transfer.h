// SPDX-License-Identifier: MIT

#ifndef P4_USB_CONTENT_TRANSFER_H
#define P4_USB_CONTENT_TRANSFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_CONTENT_TRANSFER_BAUD = 921600,
    P4_CONTENT_TRANSFER_CHUNK_BYTES = 4096,
};

typedef enum {
    P4_CONTENT_TRANSFER_IDLE = 0,
    P4_CONTENT_TRANSFER_RECEIVING,
    P4_CONTENT_TRANSFER_INSTALLED,
    P4_CONTENT_TRANSFER_FAILED,
} p4_content_transfer_state_t;

typedef struct {
    p4_content_transfer_state_t state;
    uint32_t received_bytes;
    uint32_t expected_bytes;
    uint8_t progress_percent;
    uint8_t last_status;
    bool ready;
    bool busy;
} p4_content_transfer_info_t;

typedef esp_err_t (*p4_content_transfer_send_fn)(
    void *context, const uint8_t *bytes, size_t bytes_length);
typedef esp_err_t (*p4_content_transfer_wait_tx_fn)(
    void *context, uint32_t timeout_ms);
typedef esp_err_t (*p4_content_transfer_set_baud_fn)(
    void *context, uint32_t baudrate);

typedef struct {
    p4_content_transfer_send_fn send;
    p4_content_transfer_wait_tx_fn wait_tx;
    p4_content_transfer_set_baud_fn set_baud;
    void *context;
    uint32_t idle_baud;
} p4_content_transfer_transport_t;

/**
 * Start the bounded Console OS content receiver on the programming UART.
 *
 * The physical user transport is the board's H1 USB-C programming port and
 * CH343 USB-UART bridge. This service does not claim the P4 USB host pins.
 */
esp_err_t p4_content_transfer_init(
    const char *mounted_storage_root,
    const p4_content_transfer_transport_t *transport);

/** Allow or reject new manifests without interrupting an accepted transfer. */
void p4_content_transfer_set_available(bool available);

/**
 * Feed one raw H1 receive block. True means an accepted transfer owns the
 * block and multiplayer framing must remain suspended until reboot.
 */
bool p4_content_transfer_consume(
    const uint8_t *bytes, size_t bytes_length);

/** Advance the non-blocking receiver. Call at least every 20 ms. */
void p4_content_transfer_poll(void);

/** Return a coherent status snapshot for the Console OS Library page. */
p4_content_transfer_info_t p4_content_transfer_info(void);

#ifdef __cplusplus
}
#endif

#endif
