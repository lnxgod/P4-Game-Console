#ifndef PLATFORM_STORAGE_PROBE_H
#define PLATFORM_STORAGE_PROBE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_STORAGE_PROBE_ID_SHA256_HEX_LENGTH 64U
#define PLATFORM_STORAGE_PROBE_FS_NOT_RUN (-1)

typedef struct {
    uint32_t requested_frequency_khz;
    int32_t real_frequency_khz;
    bool one_bit_only;
    bool ddr_host_capability_disabled;

    esp_err_t host_init_result;
    esp_err_t slot_init_result;
    esp_err_t card_init_result;
    esp_err_t card_status_result;
    esp_err_t raw_read_result;
    esp_err_t filesystem_io_result;
    esp_err_t cleanup_result;

    bool card_identity_valid;
    char card_identity_sha256[PLATFORM_STORAGE_PROBE_ID_SHA256_HEX_LENGTH + 1U];
    uint64_t capacity_bytes;
    uint32_t sector_size_bytes;
    uint32_t raw_reads_completed;
    bool raw_reads_stable;

    bool mbr_signature_present;
    uint32_t valid_mbr_partition_count;
    uint32_t first_partition_type;
    uint64_t first_partition_lba;

    bool filesystem_attempted;
    int32_t filesystem_result;
    uint32_t filesystem_reads_completed;
    uint32_t filesystem_blocked_write_attempts;
} platform_storage_probe_report_t;

/**
 * Run one fully read-only SDMMC characterization profile.
 *
 * The only accepted wiring is slot 0, one-bit, GPIO43/44/39 with internal
 * pull-ups. The requested clock must be between 400 kHz and 10 MHz. DDR,
 * D1-D7, card detect, and write protect are disabled. Raw sectors are read
 * repeatedly and FatFs receives a custom disk adapter whose write callback
 * always returns RES_WRPRT. No file, sector, partition, erase, or format write
 * API is called.
 *
 * ESP_OK means the host/card initialized, repeated raw reads were stable, and
 * an exact FatFs result was obtained. That result can still be a semantic
 * filesystem error such as FR_NO_FILESYSTEM and is reported separately.
 */
esp_err_t platform_storage_probe_run_profile(
    uint32_t requested_frequency_khz,
    platform_storage_probe_report_t *out_report
);

/** Return a stable name for a FatFs FRESULT numeric value. */
const char *platform_storage_probe_filesystem_result_name(int32_t result);

#ifdef __cplusplus
}
#endif

#endif
