// SPDX-License-Identifier: MIT

#ifndef P4_MULTIPLAYER_UART_H
#define P4_MULTIPLAYER_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    /* The first H1 relay is point-to-point: one wire route, one remote peer. */
    P4_MP_UART_RELAY_ROUTE_ID = 1,
    P4_MP_UART_RAW_TX_MAX_BYTES = 64,
};

typedef void (*p4_mp_uart_frame_handler_t)(
    void *context,
    uint64_t route_id,
    const uint8_t *datagram,
    size_t datagram_length);

/**
 * Observe one bounded H1 receive block before P4MP framing. Return true only
 * after the block has been claimed by another OS-owned protocol. This keeps a
 * single UART driver owner while allowing a negotiated provisioning session
 * to temporarily suspend multiplayer framing.
 */
typedef bool (*p4_mp_uart_raw_handler_t)(
    void *context,
    const uint8_t *bytes,
    size_t bytes_length);

typedef struct {
    bool ready;
    uint32_t baudrate;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t discarded_bytes;
    uint32_t dropped_frames;
    esp_err_t last_error;
} p4_mp_uart_status_t;

/** Claim the configured console UART receiver without changing its baud. */
esp_err_t p4_mp_uart_endpoint_init(
    p4_mp_uart_frame_handler_t handler,
    void *handler_context);

/** Rebind delivery during an OS-owned foreground-game handoff. */
esp_err_t p4_mp_uart_endpoint_set_handler(
    p4_mp_uart_frame_handler_t handler,
    void *handler_context);

/** Register or clear the OS-owned raw protocol observer. */
esp_err_t p4_mp_uart_endpoint_set_raw_handler(
    p4_mp_uart_raw_handler_t handler,
    void *handler_context);

/**
 * Drain a bounded amount of UART input and deliver complete CRC-checked P4MP
 * datagrams on the caller's task. No callback runs from an ISR or worker.
 */
void p4_mp_uart_endpoint_poll(void);

/** Write exactly one already-valid P4MP datagram to the H1 host relay. */
esp_err_t p4_mp_uart_endpoint_send(
    const uint8_t *datagram,
    size_t datagram_length);

/** Send one small control response for an OS-owned raw protocol. */
esp_err_t p4_mp_uart_endpoint_send_raw(
    const uint8_t *bytes,
    size_t bytes_length);

/** Wait until queued H1 bytes have left the UART before changing baud. */
esp_err_t p4_mp_uart_endpoint_wait_tx_done(uint32_t timeout_ms);

/** Change H1 baud for a negotiated session; reboot restores sdkconfig baud. */
esp_err_t p4_mp_uart_endpoint_set_baudrate(uint32_t baudrate);

/** Return an atomic-enough single-task status snapshot. */
p4_mp_uart_status_t p4_mp_uart_endpoint_status(void);

#ifdef __cplusplus
}
#endif

#endif
