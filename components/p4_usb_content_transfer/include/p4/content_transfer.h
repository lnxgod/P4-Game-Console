// SPDX-License-Identifier: MIT

#ifndef P4_USB_CONTENT_TRANSFER_H
#define P4_USB_CONTENT_TRANSFER_H

#include <stdbool.h>
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

/**
 * Start the bounded Console OS content receiver on the programming UART.
 *
 * The physical user transport is the board's H1 USB-C programming port and
 * CH343 USB-UART bridge. This service does not claim the P4 USB host pins.
 */
esp_err_t p4_content_transfer_init(const char *mounted_storage_root);

/** Advance the non-blocking receiver. Call at least every 20 ms. */
void p4_content_transfer_poll(void);

/** Return a coherent status snapshot for the Console OS Library page. */
p4_content_transfer_info_t p4_content_transfer_info(void);

#ifdef __cplusplus
}
#endif

#endif
