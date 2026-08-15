// SPDX-License-Identifier: MIT

#include "platform_game_storage_internal.h"

#include <inttypes.h>
#include <stdint.h>

#include "class/msc/msc.h"
#include "class/msc/msc_device.h"
#include "esp_err.h"
#include "esp_log.h"

enum {
    SCSI_ASC_LOGICAL_BLOCK_OUT_OF_RANGE = 0x21,
    SCSI_ASC_LOGICAL_UNIT_NOT_READY = 0x04,
    SCSI_ASC_WRITE_ERROR = 0x0c,
};

static const char *const TAG = "p4_msc_write";

/*
 * Supplied by GNU ld's --wrap=tud_msc_write10_cb transformation. The pinned
 * esp_tinyusb 2.0.1 implementation accepts a transfer, copies it into one
 * reusable buffer, and only then defers the actual flash write. A second
 * transfer can overwrite that buffer before the first deferred call runs.
 *
 * This wrapper deliberately performs a synchronous, verified write. TinyUSB
 * does not arm the next OUT transfer until this callback returns, so the DMA
 * buffer stays immutable and write errors reach the host as a failed command.
 */
int32_t __wrap_tud_msc_write10_cb(uint8_t lun, uint32_t lba,
                                  uint32_t offset, uint8_t *buffer,
                                  uint32_t bufsize)
{
    const esp_err_t result = platform_game_storage_msc_write10(
        lun, lba, offset, buffer, (size_t)bufsize);
    if (result == ESP_OK) {
        return (int32_t)bufsize;
    }

    uint8_t sense = SCSI_SENSE_MEDIUM_ERROR;
    uint8_t asc = SCSI_ASC_WRITE_ERROR;
    if (result == ESP_ERR_INVALID_ARG || result == ESP_ERR_INVALID_SIZE) {
        sense = SCSI_SENSE_ILLEGAL_REQUEST;
        asc = SCSI_ASC_LOGICAL_BLOCK_OUT_OF_RANGE;
    } else if (result == ESP_ERR_INVALID_STATE) {
        sense = SCSI_SENSE_NOT_READY;
        asc = SCSI_ASC_LOGICAL_UNIT_NOT_READY;
    }
    (void)tud_msc_set_sense(lun, sense, asc, 0U);
    ESP_LOGE(TAG,
             "verified WRITE(10) failed lun=%u lba=%" PRIu32
             " offset=%" PRIu32 " bytes=%" PRIu32 " error=%s",
             (unsigned)lun, lba, offset, bufsize, esp_err_to_name(result));
    return TUD_MSC_RET_ERROR;
}
