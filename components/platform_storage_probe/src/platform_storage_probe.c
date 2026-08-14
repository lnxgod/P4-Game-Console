#include "platform/storage_probe.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "diskio_impl.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "ff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "sdmmc_cmd.h"

#define PROBE_SDMMC_SLOT SDMMC_HOST_SLOT_0
#define PROBE_SDMMC_BUS_WIDTH 1
#define PROBE_SDMMC_CLK GPIO_NUM_43
#define PROBE_SDMMC_CMD GPIO_NUM_44
#define PROBE_SDMMC_D0 GPIO_NUM_39
#define PROBE_MIN_FREQUENCY_KHZ 400U
#define PROBE_MAX_FREQUENCY_KHZ 10000U
#define PROBE_SECTOR_BYTES 512U
#define PROBE_SECTOR_ZERO_REPEATS 8U
#define PROBE_OTHER_SECTOR_REPEATS 2U
#define PROBE_DMA_ALIGNMENT 64U
#define PROBE_MBR_SIGNATURE_OFFSET 510U
#define PROBE_MBR_PARTITION_TABLE_OFFSET 446U
#define PROBE_MBR_PARTITION_ENTRY_BYTES 16U
#define PROBE_MBR_PARTITION_COUNT 4U

typedef struct {
    sdmmc_card_t *card;
    uint32_t reads_completed;
    uint32_t blocked_write_attempts;
    esp_err_t last_io_result;
} probe_disk_context_t;

static probe_disk_context_t s_probe_disk;

static void write_u32_be(uint8_t output[4], uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

static uint32_t read_u32_le(const uint8_t input[4])
{
    return (uint32_t)input[0]
        | ((uint32_t)input[1] << 8U)
        | ((uint32_t)input[2] << 16U)
        | ((uint32_t)input[3] << 24U);
}

static void digest_to_hex(
    const uint8_t digest[32],
    char output[PLATFORM_STORAGE_PROBE_ID_SHA256_HEX_LENGTH + 1U]
)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t index = 0U; index < 32U; ++index) {
        output[index * 2U] = hex[digest[index] >> 4U];
        output[index * 2U + 1U] = hex[digest[index] & 0x0fU];
    }
    output[PLATFORM_STORAGE_PROBE_ID_SHA256_HEX_LENGTH] = '\0';
}

static esp_err_t hash_card_identity(
    const sdmmc_card_t *card,
    char output[PLATFORM_STORAGE_PROBE_ID_SHA256_HEX_LENGTH + 1U]
)
{
    if (card == NULL || output == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t material[40] = {0};
    size_t offset = 0U;
#define APPEND_U32(value_) do { \
        write_u32_be(&material[offset], (uint32_t)(value_)); \
        offset += 4U; \
    } while (0)
    APPEND_U32(card->cid.mfg_id);
    APPEND_U32(card->cid.oem_id);
    memcpy(&material[offset], card->cid.name, sizeof(card->cid.name));
    offset += sizeof(card->cid.name);
    APPEND_U32(card->cid.revision);
    APPEND_U32(card->cid.serial);
    APPEND_U32(card->cid.date);
    APPEND_U32(card->ocr);
    APPEND_U32(card->csd.capacity);
    APPEND_U32(card->csd.sector_size);
#undef APPEND_U32
    if (offset != sizeof(material)) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t digest[32];
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    int crypto_result = mbedtls_sha256_starts(&context, 0);
    if (crypto_result == 0) {
        crypto_result = mbedtls_sha256_update(&context, material, sizeof(material));
    }
    if (crypto_result == 0) {
        crypto_result = mbedtls_sha256_finish(&context, digest);
    }
    mbedtls_sha256_free(&context);
    if (crypto_result != 0) {
        return ESP_FAIL;
    }
    digest_to_hex(digest, output);
    return ESP_OK;
}

static void inspect_mbr(
    const uint8_t sector[PROBE_SECTOR_BYTES],
    uint64_t capacity_sectors,
    platform_storage_probe_report_t *report
)
{
    report->mbr_signature_present =
        sector[PROBE_MBR_SIGNATURE_OFFSET] == 0x55U
        && sector[PROBE_MBR_SIGNATURE_OFFSET + 1U] == 0xaaU;
    if (!report->mbr_signature_present) {
        return;
    }

    for (size_t index = 0U; index < PROBE_MBR_PARTITION_COUNT; ++index) {
        const size_t entry_offset = PROBE_MBR_PARTITION_TABLE_OFFSET
            + index * PROBE_MBR_PARTITION_ENTRY_BYTES;
        const uint32_t type = sector[entry_offset + 4U];
        const uint32_t first_lba = read_u32_le(&sector[entry_offset + 8U]);
        const uint32_t sector_count = read_u32_le(&sector[entry_offset + 12U]);
        const uint64_t end_lba = (uint64_t)first_lba + (uint64_t)sector_count;
        if (type == 0U || sector_count == 0U || end_lba > capacity_sectors) {
            continue;
        }
        report->valid_mbr_partition_count++;
        if (report->first_partition_lba == 0U) {
            report->first_partition_type = type;
            report->first_partition_lba = first_lba;
        }
    }
}

static esp_err_t read_sector_stable(
    sdmmc_card_t *card,
    uint64_t sector,
    uint32_t repeats,
    uint8_t *baseline,
    uint8_t *scratch,
    uint32_t *reads_completed
)
{
    if (card == NULL || baseline == NULL || scratch == NULL || reads_completed == NULL
        || repeats == 0U || sector > (uint64_t)SIZE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = sdmmc_read_sectors(card, baseline, (size_t)sector, 1U);
    if (result != ESP_OK) {
        return result;
    }
    (*reads_completed)++;
    for (uint32_t attempt = 1U; attempt < repeats; ++attempt) {
        result = sdmmc_read_sectors(card, scratch, (size_t)sector, 1U);
        if (result != ESP_OK) {
            return result;
        }
        (*reads_completed)++;
        if (memcmp(baseline, scratch, PROBE_SECTOR_BYTES) != 0) {
            return ESP_ERR_INVALID_CRC;
        }
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
    return ESP_OK;
}

static DSTATUS probe_disk_initialize(BYTE physical_drive)
{
    return physical_drive == 0U && s_probe_disk.card != NULL ? 0U : STA_NOINIT;
}

static DSTATUS probe_disk_status(BYTE physical_drive)
{
    return physical_drive == 0U && s_probe_disk.card != NULL ? 0U : STA_NOINIT;
}

static DRESULT probe_disk_read(BYTE physical_drive, BYTE *buffer, LBA_t sector, UINT count)
{
    if (physical_drive != 0U || s_probe_disk.card == NULL || buffer == NULL || count == 0U) {
        return RES_PARERR;
    }
    const uint64_t first = (uint64_t)sector;
    const uint64_t end = first + (uint64_t)count;
    if (end < first || end > (uint64_t)s_probe_disk.card->csd.capacity
        || first > (uint64_t)SIZE_MAX || (uint64_t)count > (uint64_t)SIZE_MAX) {
        return RES_PARERR;
    }
    s_probe_disk.last_io_result = sdmmc_read_sectors(
        s_probe_disk.card,
        buffer,
        (size_t)first,
        (size_t)count
    );
    if (s_probe_disk.last_io_result != ESP_OK) {
        return RES_ERROR;
    }
    s_probe_disk.reads_completed += count;
    return RES_OK;
}

static DRESULT probe_disk_write(
    BYTE physical_drive,
    const BYTE *buffer,
    LBA_t sector,
    UINT count
)
{
    (void)physical_drive;
    (void)buffer;
    (void)sector;
    (void)count;
    s_probe_disk.blocked_write_attempts++;
    return RES_WRPRT;
}

static DRESULT probe_disk_ioctl(BYTE physical_drive, BYTE command, void *buffer)
{
    if (physical_drive != 0U || s_probe_disk.card == NULL) {
        return RES_PARERR;
    }
    switch (command) {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT:
            if (buffer == NULL) {
                return RES_PARERR;
            }
            *(LBA_t *)buffer = (LBA_t)s_probe_disk.card->csd.capacity;
            return RES_OK;
        case GET_SECTOR_SIZE:
            if (buffer == NULL) {
                return RES_PARERR;
            }
            *(WORD *)buffer = (WORD)s_probe_disk.card->csd.sector_size;
            return RES_OK;
        case GET_BLOCK_SIZE:
            if (buffer == NULL) {
                return RES_PARERR;
            }
            *(DWORD *)buffer = 1U;
            return RES_OK;
        case CTRL_TRIM:
        case CTRL_FORMAT:
            s_probe_disk.blocked_write_attempts++;
            return RES_WRPRT;
        default:
            return RES_PARERR;
    }
}

static esp_err_t inspect_filesystem_read_only(
    sdmmc_card_t *card,
    platform_storage_probe_report_t *report
)
{
    BYTE physical_drive = FF_DRV_NOT_USED;
    esp_err_t result = ff_diskio_get_drive(&physical_drive);
    if (result != ESP_OK) {
        return result;
    }
    if (physical_drive != 0U) {
        return ESP_ERR_INVALID_STATE;
    }

    FATFS *filesystem = heap_caps_aligned_calloc(
        PROBE_DMA_ALIGNMENT,
        1U,
        sizeof(*filesystem),
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    );
    if (filesystem == NULL) {
        return ESP_ERR_NO_MEM;
    }

    static const ff_diskio_impl_t read_only_disk = {
        .init = probe_disk_initialize,
        .status = probe_disk_status,
        .read = probe_disk_read,
        .write = probe_disk_write,
        .ioctl = probe_disk_ioctl,
    };
    memset(&s_probe_disk, 0, sizeof(s_probe_disk));
    s_probe_disk.card = card;
    s_probe_disk.last_io_result = ESP_OK;
    ff_diskio_register(physical_drive, &read_only_disk);

    const char drive[] = "0:";
    report->filesystem_attempted = true;
    report->filesystem_result = (int32_t)f_mount(filesystem, drive, 1U);
    report->filesystem_reads_completed = s_probe_disk.reads_completed;
    report->filesystem_blocked_write_attempts = s_probe_disk.blocked_write_attempts;
    report->filesystem_io_result = s_probe_disk.last_io_result;

    (void)f_mount(NULL, drive, 0U);
    ff_diskio_unregister(physical_drive);
    memset(&s_probe_disk, 0, sizeof(s_probe_disk));
    heap_caps_free(filesystem);

    if (report->filesystem_blocked_write_attempts != 0U) {
        return ESP_ERR_NOT_ALLOWED;
    }
    return ESP_OK;
}

static esp_err_t run_raw_reads(
    sdmmc_card_t *card,
    platform_storage_probe_report_t *report
)
{
    if (card->csd.capacity <= 0 || card->csd.sector_size != (int)PROBE_SECTOR_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t *buffers = heap_caps_aligned_alloc(
        PROBE_DMA_ALIGNMENT,
        PROBE_SECTOR_BYTES * 2U,
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    );
    if (buffers == NULL) {
        return ESP_ERR_NO_MEM;
    }
    uint8_t *const baseline = buffers;
    uint8_t *const scratch = &buffers[PROBE_SECTOR_BYTES];

    esp_err_t result = read_sector_stable(
        card,
        0U,
        PROBE_SECTOR_ZERO_REPEATS,
        baseline,
        scratch,
        &report->raw_reads_completed
    );
    if (result != ESP_OK) {
        heap_caps_free(buffers);
        return result;
    }

    const uint64_t capacity_sectors = (uint64_t)card->csd.capacity;
    inspect_mbr(baseline, capacity_sectors, report);

    uint64_t targets[3] = {
        report->first_partition_lba,
        capacity_sectors / 2U,
        capacity_sectors - 1U,
    };
    for (size_t index = 0U; index < sizeof(targets) / sizeof(targets[0]); ++index) {
        if (targets[index] == 0U || targets[index] >= capacity_sectors) {
            continue;
        }
        bool duplicate = false;
        for (size_t prior = 0U; prior < index; ++prior) {
            if (targets[prior] == targets[index]) {
                duplicate = true;
            }
        }
        if (duplicate) {
            continue;
        }
        result = read_sector_stable(
            card,
            targets[index],
            PROBE_OTHER_SECTOR_REPEATS,
            baseline,
            scratch,
            &report->raw_reads_completed
        );
        if (result != ESP_OK) {
            heap_caps_free(buffers);
            return result;
        }
    }
    heap_caps_free(buffers);
    report->raw_reads_stable = true;
    return ESP_OK;
}

esp_err_t platform_storage_probe_run_profile(
    uint32_t requested_frequency_khz,
    platform_storage_probe_report_t *out_report
)
{
    if (out_report == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_report, 0, sizeof(*out_report));
    out_report->requested_frequency_khz = requested_frequency_khz;
    out_report->one_bit_only = true;
    out_report->ddr_host_capability_disabled = true;
    out_report->host_init_result = ESP_ERR_NOT_FINISHED;
    out_report->slot_init_result = ESP_ERR_NOT_FINISHED;
    out_report->card_init_result = ESP_ERR_NOT_FINISHED;
    out_report->card_status_result = ESP_ERR_NOT_FINISHED;
    out_report->raw_read_result = ESP_ERR_NOT_FINISHED;
    out_report->filesystem_io_result = ESP_ERR_NOT_FINISHED;
    out_report->cleanup_result = ESP_ERR_NOT_FINISHED;
    out_report->filesystem_result = PLATFORM_STORAGE_PROBE_FS_NOT_RUN;

#if !CONFIG_PLATFORM_STORAGE_PROBE_ELECROW_10_1_CROSS_REVISION_AUTHORIZED
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (requested_frequency_khz < PROBE_MIN_FREQUENCY_KHZ
        || requested_frequency_khz > PROBE_MAX_FREQUENCY_KHZ) {
        return ESP_ERR_INVALID_ARG;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT | SDMMC_HOST_FLAG_DEINIT_ARG;
    host.slot = PROBE_SDMMC_SLOT;
    host.max_freq_khz = (int)requested_frequency_khz;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = PROBE_SDMMC_CLK;
    slot.cmd = PROBE_SDMMC_CMD;
    slot.d0 = PROBE_SDMMC_D0;
    slot.d1 = GPIO_NUM_NC;
    slot.d2 = GPIO_NUM_NC;
    slot.d3 = GPIO_NUM_NC;
    slot.d4 = GPIO_NUM_NC;
    slot.d5 = GPIO_NUM_NC;
    slot.d6 = GPIO_NUM_NC;
    slot.d7 = GPIO_NUM_NC;
    slot.cd = GPIO_NUM_NC;
    slot.wp = GPIO_NUM_NC;
    slot.width = PROBE_SDMMC_BUS_WIDTH;
    slot.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    bool host_initialized = false;
    bool slot_initialized = false;
    esp_err_t result = host.init();
    out_report->host_init_result = result;
    if (result != ESP_OK) {
        out_report->cleanup_result = ESP_OK;
        return result;
    }
    host_initialized = true;

    result = sdmmc_host_init_slot(PROBE_SDMMC_SLOT, &slot);
    out_report->slot_init_result = result;
    if (result != ESP_OK) {
        goto cleanup;
    }
    slot_initialized = true;

    sdmmc_card_t card;
    memset(&card, 0, sizeof(card));
    result = sdmmc_card_init(&host, &card);
    out_report->card_init_result = result;
    if (result != ESP_OK) {
        goto cleanup;
    }
    out_report->real_frequency_khz = card.real_freq_khz;
    if (card.csd.capacity <= 0 || card.csd.sector_size <= 0) {
        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }
    out_report->capacity_bytes = (uint64_t)card.csd.capacity * (uint64_t)card.csd.sector_size;
    out_report->sector_size_bytes = (uint32_t)card.csd.sector_size;
    result = hash_card_identity(&card, out_report->card_identity_sha256);
    if (result != ESP_OK) {
        goto cleanup;
    }
    out_report->card_identity_valid = true;

    result = sdmmc_get_status(&card);
    out_report->card_status_result = result;
    if (result != ESP_OK) {
        goto cleanup;
    }

    result = run_raw_reads(&card, out_report);
    out_report->raw_read_result = result;
    if (result != ESP_OK) {
        goto cleanup;
    }

    result = inspect_filesystem_read_only(&card, out_report);
    if (result != ESP_OK) {
        goto cleanup;
    }
    result = ESP_OK;

cleanup:
    if (slot_initialized) {
        out_report->cleanup_result = sdmmc_host_deinit_slot(PROBE_SDMMC_SLOT);
    } else if (host_initialized) {
        out_report->cleanup_result = sdmmc_host_deinit();
    } else {
        out_report->cleanup_result = ESP_OK;
    }
    if (result == ESP_OK && out_report->cleanup_result != ESP_OK) {
        return out_report->cleanup_result;
    }
    return result;
#endif
}

const char *platform_storage_probe_filesystem_result_name(int32_t result)
{
    switch (result) {
        case PLATFORM_STORAGE_PROBE_FS_NOT_RUN:
            return "NOT_RUN";
        case FR_OK:
            return "FR_OK";
        case FR_DISK_ERR:
            return "FR_DISK_ERR";
        case FR_INT_ERR:
            return "FR_INT_ERR";
        case FR_NOT_READY:
            return "FR_NOT_READY";
        case FR_NO_FILE:
            return "FR_NO_FILE";
        case FR_NO_PATH:
            return "FR_NO_PATH";
        case FR_INVALID_NAME:
            return "FR_INVALID_NAME";
        case FR_DENIED:
            return "FR_DENIED";
        case FR_EXIST:
            return "FR_EXIST";
        case FR_INVALID_OBJECT:
            return "FR_INVALID_OBJECT";
        case FR_WRITE_PROTECTED:
            return "FR_WRITE_PROTECTED";
        case FR_INVALID_DRIVE:
            return "FR_INVALID_DRIVE";
        case FR_NOT_ENABLED:
            return "FR_NOT_ENABLED";
        case FR_NO_FILESYSTEM:
            return "FR_NO_FILESYSTEM";
        case FR_MKFS_ABORTED:
            return "FR_MKFS_ABORTED";
        case FR_TIMEOUT:
            return "FR_TIMEOUT";
        case FR_LOCKED:
            return "FR_LOCKED";
        case FR_NOT_ENOUGH_CORE:
            return "FR_NOT_ENOUGH_CORE";
        case FR_TOO_MANY_OPEN_FILES:
            return "FR_TOO_MANY_OPEN_FILES";
        case FR_INVALID_PARAMETER:
            return "FR_INVALID_PARAMETER";
        default:
            return "FR_UNKNOWN";
    }
}
