// SPDX-License-Identifier: MIT

#ifndef P4_FILE_TRANSFER_H
#define P4_FILE_TRANSFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "p4/content_transfer.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_FILE_TRANSFER_PROTOCOL_VERSION = 1,
    P4_FILE_TRANSFER_NAME_BYTES = 40,
    P4_FILE_TRANSFER_CHUNK_BYTES = 4096,
    P4_FILE_TRANSFER_EXCHANGE_MAX_BYTES = 8 * 1024 * 1024,
    P4_FILE_TRANSFER_FLAG_REPLACE = 1,
};

typedef enum {
    P4_FILE_TRANSFER_STATUS_OK = 0,
    P4_FILE_TRANSFER_STATUS_BAD_REQUEST = 1,
    P4_FILE_TRANSFER_STATUS_STORAGE = 2,
    P4_FILE_TRANSFER_STATUS_OCCUPIED = 3,
    P4_FILE_TRANSFER_STATUS_IO = 4,
    P4_FILE_TRANSFER_STATUS_SEQUENCE = 5,
    P4_FILE_TRANSFER_STATUS_CRC = 6,
    P4_FILE_TRANSFER_STATUS_HASH = 7,
    P4_FILE_TRANSFER_STATUS_TIMEOUT = 8,
    P4_FILE_TRANSFER_STATUS_UNSUPPORTED = 9,
    P4_FILE_TRANSFER_STATUS_ALREADY_PRESENT = 10,
    P4_FILE_TRANSFER_STATUS_BAD_NAME = 11,
    P4_FILE_TRANSFER_STATUS_BAD_PACKAGE = 12,
    P4_FILE_TRANSFER_STATUS_NOT_FOUND = 13,
    P4_FILE_TRANSFER_STATUS_TOO_LARGE = 14,
    P4_FILE_TRANSFER_STATUS_BUSY = 15,
} p4_file_transfer_status_t;

typedef enum {
    P4_FILE_TRANSFER_DIRECTION_NONE = 0,
    P4_FILE_TRANSFER_UPLOAD,
    P4_FILE_TRANSFER_DOWNLOAD,
} p4_file_transfer_direction_t;

typedef enum {
    P4_FILE_TRANSFER_CLASS_NONE = 0,
    /** Validated native P4GAME1 cartridge under /GAMES. */
    P4_FILE_TRANSFER_CLASS_P4G = 1,
    /** Opaque hash-checked exchange file under /TRANSFER. */
    P4_FILE_TRANSFER_CLASS_EXCHANGE = 2,
} p4_file_transfer_class_t;

typedef enum {
    P4_FILE_TRANSFER_IDLE = 0,
    P4_FILE_TRANSFER_RECEIVING,
    P4_FILE_TRANSFER_SENDING,
    P4_FILE_TRANSFER_COMPLETE,
    P4_FILE_TRANSFER_FAILED,
} p4_file_transfer_state_t;

typedef struct {
    p4_file_transfer_state_t state;
    p4_file_transfer_direction_t direction;
    p4_file_transfer_class_t file_class;
    uint32_t transferred_bytes;
    uint32_t total_bytes;
    uint32_t generation;
    uint8_t progress_percent;
    uint8_t last_status;
    char file_name[P4_FILE_TRANSFER_NAME_BYTES];
    bool ready;
    bool busy;
} p4_file_transfer_info_t;

/**
 * Initialize the transport-neutral, bounded file transaction service.
 *
 * H1 supplies the same physical transport callbacks as provisioning. BLE may
 * bind the same framed protocol only while the File Transfer app owns BLE;
 * games and Doom never share a live transfer session.
 */
esp_err_t p4_file_transfer_init(
    const char *mounted_storage_root,
    const p4_content_transfer_transport_t *transport);

/** Allow or reject a new transaction without interrupting an active one. */
void p4_file_transfer_set_available(bool available);

/** Consume an untrusted raw transport block. True means this service owns it. */
bool p4_file_transfer_consume(const uint8_t *bytes, size_t bytes_length);

/** Advance download transmission and timeout handling without blocking. */
void p4_file_transfer_poll(void);

/** Return a coherent UI/status snapshot. */
p4_file_transfer_info_t p4_file_transfer_info(void);

#ifdef __cplusplus
}
#endif

#endif
