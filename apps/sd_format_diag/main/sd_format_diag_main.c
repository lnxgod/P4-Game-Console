#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "mbedtls/sha256.h"
#include "sdmmc_cmd.h"

#define FORMAT_SDMMC_SLOT SDMMC_HOST_SLOT_0
#define FORMAT_SDMMC_BUS_WIDTH 1
#define FORMAT_SDMMC_CLK GPIO_NUM_43
#define FORMAT_SDMMC_CMD GPIO_NUM_44
#define FORMAT_SDMMC_D0 GPIO_NUM_39
#define FORMAT_FREQUENCY_KHZ 1000
#define FORMAT_ALLOCATION_UNIT_BYTES (16U * 1024U)
#define FORMAT_MOUNT_POINT "/sdcard"
#define FORMAT_VERIFY_PATH FORMAT_MOUNT_POINT "/.p4-format-verify.bin"
#define FORMAT_VERIFY_BYTES 4096U
#define FORMAT_SHA256_HEX_LENGTH 64U
#define FORMAT_WORK_BUFFER_BYTES 4096U

static const char *const TAG = "p4_sd_format_diag";

static void digest_to_hex(
    const uint8_t digest[32],
    char output[FORMAT_SHA256_HEX_LENGTH + 1U]
)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t index = 0U; index < 32U; ++index) {
        output[index * 2U] = hex[digest[index] >> 4U];
        output[index * 2U + 1U] = hex[digest[index] & 0x0fU];
    }
    output[FORMAT_SHA256_HEX_LENGTH] = '\0';
}

static esp_err_t sha256_bytes(
    const uint8_t *bytes,
    size_t length,
    char output[FORMAT_SHA256_HEX_LENGTH + 1U]
)
{
    if (bytes == NULL || output == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t digest[32];
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    int result = mbedtls_sha256_starts(&context, 0);
    if (result == 0) {
        result = mbedtls_sha256_update(&context, bytes, length);
    }
    if (result == 0) {
        result = mbedtls_sha256_finish(&context, digest);
    }
    mbedtls_sha256_free(&context);
    if (result != 0) {
        return ESP_FAIL;
    }
    digest_to_hex(digest, output);
    return ESP_OK;
}

static void fill_verify_pattern(uint8_t bytes[FORMAT_VERIFY_BYTES])
{
    uint32_t state = UINT32_C(0x50344641);
    for (size_t index = 0U; index < FORMAT_VERIFY_BYTES; ++index) {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        bytes[index] = (uint8_t)(state >> 24U);
    }
}

static esp_err_t verify_write_read_remove(char expected_sha256[FORMAT_SHA256_HEX_LENGTH + 1U])
{
    uint8_t *buffers = heap_caps_malloc(
        FORMAT_VERIFY_BYTES * 2U,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    );
    if (buffers == NULL) {
        return ESP_ERR_NO_MEM;
    }
    uint8_t *const expected = buffers;
    uint8_t *const actual = &buffers[FORMAT_VERIFY_BYTES];
    fill_verify_pattern(expected);

    esp_err_t result = sha256_bytes(expected, FORMAT_VERIFY_BYTES, expected_sha256);
    if (result != ESP_OK) {
        heap_caps_free(buffers);
        return result;
    }

    FILE *file = fopen(FORMAT_VERIFY_PATH, "wb");
    if (file == NULL) {
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 VERIFY_FAIL stage=open-write errno=%d", errno);
        heap_caps_free(buffers);
        return ESP_FAIL;
    }
    const size_t written = fwrite(expected, 1U, FORMAT_VERIFY_BYTES, file);
    if (written != FORMAT_VERIFY_BYTES || fflush(file) != 0 || fsync(fileno(file)) != 0) {
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 VERIFY_FAIL stage=durable-write bytes=%u errno=%d",
                 (unsigned)written, errno);
        (void)fclose(file);
        heap_caps_free(buffers);
        return ESP_FAIL;
    }
    if (fclose(file) != 0) {
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 VERIFY_FAIL stage=close-write errno=%d", errno);
        heap_caps_free(buffers);
        return ESP_FAIL;
    }

    memset(actual, 0, FORMAT_VERIFY_BYTES);
    file = fopen(FORMAT_VERIFY_PATH, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 VERIFY_FAIL stage=open-read errno=%d", errno);
        heap_caps_free(buffers);
        return ESP_FAIL;
    }
    const size_t read = fread(actual, 1U, FORMAT_VERIFY_BYTES, file);
    const int trailing = fgetc(file);
    if (fclose(file) != 0 || read != FORMAT_VERIFY_BYTES || trailing != EOF
        || memcmp(expected, actual, FORMAT_VERIFY_BYTES) != 0) {
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 VERIFY_FAIL stage=readback bytes=%u",
                 (unsigned)read);
        heap_caps_free(buffers);
        return ESP_FAIL;
    }
    if (unlink(FORMAT_VERIFY_PATH) != 0) {
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 VERIFY_FAIL stage=remove errno=%d", errno);
        heap_caps_free(buffers);
        return ESP_FAIL;
    }
    heap_caps_free(buffers);
    return ESP_OK;
}

static esp_err_t verify_file_absent(void)
{
    errno = 0;
    struct stat status;
    if (stat(FORMAT_VERIFY_PATH, &status) == 0 || errno != ENOENT) {
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 VERIFY_FAIL stage=removed-file-persists errno=%d", errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static uint16_t read_u16_le(const uint8_t input[2])
{
    return (uint16_t)input[0] | (uint16_t)((uint16_t)input[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t input[4])
{
    return (uint32_t)input[0]
        | ((uint32_t)input[1] << 8U)
        | ((uint32_t)input[2] << 16U)
        | ((uint32_t)input[3] << 24U);
}

static bool is_power_of_two_u8(uint8_t value)
{
    return value != 0U && (value & (uint8_t)(value - 1U)) == 0U;
}

static esp_err_t verify_fat32_disk_layout(
    sdmmc_card_t *card,
    uint64_t *out_partition_lba,
    uint64_t *out_partition_sectors
)
{
    if (card == NULL || out_partition_lba == NULL || out_partition_sectors == NULL
        || card->csd.capacity <= 0 || card->csd.sector_size != 512) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t *sectors = heap_caps_aligned_alloc(
        64U,
        1024U,
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    );
    if (sectors == NULL) {
        return ESP_ERR_NO_MEM;
    }
    uint8_t *const mbr = sectors;
    uint8_t *const vbr = &sectors[512U];
    esp_err_t result = sdmmc_read_sectors(card, mbr, 0U, 1U);
    if (result != ESP_OK) {
        goto cleanup;
    }

    const uint8_t *const first = &mbr[446U];
    const uint32_t partition_lba = read_u32_le(&first[8U]);
    const uint32_t partition_sectors = read_u32_le(&first[12U]);
    const uint64_t partition_end = (uint64_t)partition_lba + (uint64_t)partition_sectors;
    bool extra_partition = false;
    for (size_t index = 1U; index < 4U; ++index) {
        const uint8_t *const entry = &mbr[446U + index * 16U];
        for (size_t byte = 0U; byte < 16U; ++byte) {
            if (entry[byte] != 0U) {
                extra_partition = true;
            }
        }
        if (entry[4U] != 0U || read_u32_le(&entry[12U]) != 0U) {
            extra_partition = true;
        }
    }
    if (mbr[510U] != 0x55U || mbr[511U] != 0xaaU
        || (first[0U] != 0x00U && first[0U] != 0x80U)
        || (first[4U] != 0x0bU && first[4U] != 0x0cU)
        || partition_lba == 0U || partition_sectors == 0U || extra_partition
        || partition_end != (uint64_t)card->csd.capacity) {
        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    result = sdmmc_read_sectors(card, vbr, partition_lba, 1U);
    if (result != ESP_OK) {
        goto cleanup;
    }
    const uint16_t bytes_per_sector = read_u16_le(&vbr[11U]);
    const uint8_t sectors_per_cluster = vbr[13U];
    const uint16_t reserved_sectors = read_u16_le(&vbr[14U]);
    const uint8_t fat_count = vbr[16U];
    const uint16_t root_entries = read_u16_le(&vbr[17U]);
    const uint16_t fat16_sectors = read_u16_le(&vbr[22U]);
    const uint32_t total_sectors = read_u32_le(&vbr[32U]);
    const uint32_t fat32_sectors = read_u32_le(&vbr[36U]);
    const uint32_t root_cluster = read_u32_le(&vbr[44U]);
    const uint32_t hidden_sectors = read_u32_le(&vbr[28U]);
    const uint64_t metadata_sectors = (uint64_t)reserved_sectors
        + (uint64_t)fat_count * (uint64_t)fat32_sectors;
    const uint64_t data_sectors = total_sectors > metadata_sectors
        ? (uint64_t)total_sectors - metadata_sectors
        : 0U;
    const uint64_t cluster_count = sectors_per_cluster != 0U
        ? data_sectors / sectors_per_cluster
        : 0U;
    if (vbr[510U] != 0x55U || vbr[511U] != 0xaaU
        || bytes_per_sector != 512U || !is_power_of_two_u8(sectors_per_cluster)
        || sectors_per_cluster != FORMAT_ALLOCATION_UNIT_BYTES / 512U
        || reserved_sectors == 0U || fat_count != 2U || root_entries != 0U
        || fat16_sectors != 0U || total_sectors == 0U || fat32_sectors == 0U
        || root_cluster < 2U || hidden_sectors != partition_lba
        || total_sectors != partition_sectors
        || (uint64_t)partition_lba + (uint64_t)total_sectors != partition_end
        || cluster_count < 65525U) {
        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    *out_partition_lba = partition_lba;
    *out_partition_sectors = partition_sectors;
    ESP_LOGI(
        TAG,
        "P4_SD_FORMAT_DIAG D1 LAYOUT_PASS mbr=true partition_type=0x%02x"
        " partition_lba=%" PRIu64 " partition_sectors=%" PRIu64
        " vbr=true fs=FAT32 fats=2 bytes_per_sector=512 sectors_per_cluster=%u",
        first[4U],
        *out_partition_lba,
        *out_partition_sectors,
        (unsigned)sectors_per_cluster
    );
    result = ESP_OK;

cleanup:
    heap_caps_free(sectors);
    return result;
}

static sdmmc_host_t make_host(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT | SDMMC_HOST_FLAG_DEINIT_ARG;
    host.slot = FORMAT_SDMMC_SLOT;
    host.max_freq_khz = FORMAT_FREQUENCY_KHZ;
    return host;
}

static sdmmc_slot_config_t make_slot(void)
{
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = FORMAT_SDMMC_CLK;
    slot.cmd = FORMAT_SDMMC_CMD;
    slot.d0 = FORMAT_SDMMC_D0;
    slot.d1 = GPIO_NUM_NC;
    slot.d2 = GPIO_NUM_NC;
    slot.d3 = GPIO_NUM_NC;
    slot.d4 = GPIO_NUM_NC;
    slot.d5 = GPIO_NUM_NC;
    slot.d6 = GPIO_NUM_NC;
    slot.d7 = GPIO_NUM_NC;
    slot.cd = GPIO_NUM_NC;
    slot.wp = GPIO_NUM_NC;
    slot.width = FORMAT_SDMMC_BUS_WIDTH;
    slot.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    return slot;
}

static esp_err_t mount_card(bool format_if_mount_failed, sdmmc_card_t **out_card)
{
    sdmmc_host_t host = make_host();
    sdmmc_slot_config_t slot = make_slot();
    const esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = format_if_mount_failed,
        .max_files = 2,
        .allocation_unit_size = FORMAT_ALLOCATION_UNIT_BYTES,
        .disk_status_check_enable = true,
        .use_one_fat = false,
    };
    return esp_vfs_fat_sdmmc_mount(FORMAT_MOUNT_POINT, &host, &slot, &mount, out_card);
}

static esp_err_t force_partition_and_fat32(
    sdmmc_card_t *card,
    uint8_t *out_filesystem_type
)
{
    if (card == NULL || out_filesystem_type == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_filesystem_type = 0U;

    BYTE physical_drive = FF_DRV_NOT_USED;
    esp_err_t result = ff_diskio_get_drive(&physical_drive);
    if (result != ESP_OK || physical_drive == FF_DRV_NOT_USED || physical_drive > 9U) {
        return result == ESP_OK ? ESP_ERR_INVALID_STATE : result;
    }
    ff_diskio_register_sdmmc(physical_drive, card);
    ff_sdmmc_set_disk_status_check(physical_drive, true);

    void *work = malloc(FORMAT_WORK_BUFFER_BYTES);
    if (work == NULL) {
        ff_diskio_unregister(physical_drive);
        return ESP_ERR_NO_MEM;
    }

    const LBA_t partitions[] = {100U, 0U, 0U, 0U};
    FRESULT fat_result = f_fdisk(physical_drive, partitions, work);
    ESP_LOGI(TAG, "P4_SD_FORMAT_DIAG D1 PARTITION_RESULT fresult=%d", (int)fat_result);
    if (fat_result != FR_OK) {
        result = ESP_FAIL;
        goto cleanup;
    }

    const char drive[] = {(char)('0' + physical_drive), ':', '\0'};
    const MKFS_PARM options = {
        .fmt = FM_FAT32,
        .n_fat = 2U,
        .align = 0U,
        .n_root = 0U,
        .au_size = FORMAT_ALLOCATION_UNIT_BYTES,
    };
    fat_result = f_mkfs(drive, &options, work, FORMAT_WORK_BUFFER_BYTES);
    ESP_LOGI(TAG, "P4_SD_FORMAT_DIAG D1 MKFS_RESULT requested=FAT32 fresult=%d", (int)fat_result);
    if (fat_result != FR_OK) {
        result = ESP_FAIL;
        goto cleanup;
    }

    FATFS *filesystem = heap_caps_calloc(
        1U,
        sizeof(*filesystem),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    );
    if (filesystem == NULL) {
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    fat_result = f_mount(filesystem, drive, 1U);
    if (fat_result == FR_OK) {
        *out_filesystem_type = filesystem->fs_type;
    }
    ESP_LOGI(TAG, "P4_SD_FORMAT_DIAG D1 FAT_TYPE_RESULT fresult=%d fs_type=%u expected=%u",
             (int)fat_result, (unsigned)*out_filesystem_type, (unsigned)FS_FAT32);
    const FRESULT unmount_result = f_mount(NULL, drive, 0U);
    heap_caps_free(filesystem);
    filesystem = NULL;
    if (fat_result != FR_OK || unmount_result != FR_OK || *out_filesystem_type != FS_FAT32) {
        result = ESP_FAIL;
        goto cleanup;
    }
    uint64_t partition_lba = 0U;
    uint64_t partition_sectors = 0U;
    result = verify_fat32_disk_layout(card, &partition_lba, &partition_sectors);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 LAYOUT_FAIL result=%s", esp_err_to_name(result));
        goto cleanup;
    }
    result = ESP_OK;

cleanup:
    free(work);
    ff_diskio_unregister(physical_drive);
    return result;
}

void app_main(void)
{
#if !CONFIG_SD_FORMAT_DIAG_DESTRUCTIVE_AUTHORIZED
    ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 BLOCKED reason=destructive-build-authorization-disabled");
    return;
#else
    ESP_LOGW(
        TAG,
        "P4_SD_FORMAT_DIAG D1 DESTRUCTIVE_START target=sdmmc0-j5"
        " pins=43,44,39 width=1 requested_khz=1000 action=erase-repartition-fat32"
    );

    sdmmc_host_t host = make_host();
    sdmmc_slot_config_t slot = make_slot();
    esp_err_t result = host.init();
    ESP_LOGI(TAG, "P4_SD_FORMAT_DIAG D1 HOST_INIT result=%s", esp_err_to_name(result));
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 FAIL stage=host-init result=%s", esp_err_to_name(result));
        return;
    }
    result = sdmmc_host_init_slot(FORMAT_SDMMC_SLOT, &slot);
    ESP_LOGI(TAG, "P4_SD_FORMAT_DIAG D1 SLOT_INIT result=%s", esp_err_to_name(result));
    if (result != ESP_OK) {
        (void)sdmmc_host_deinit();
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 FAIL stage=slot-init result=%s", esp_err_to_name(result));
        return;
    }
    sdmmc_card_t *raw_card = heap_caps_calloc(
        1U,
        sizeof(*raw_card),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    );
    if (raw_card == NULL) {
        (void)sdmmc_host_deinit_slot(FORMAT_SDMMC_SLOT);
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 FAIL stage=card-allocation result=%s",
                 esp_err_to_name(ESP_ERR_NO_MEM));
        return;
    }
    result = sdmmc_card_init(&host, raw_card);
    ESP_LOGI(TAG, "P4_SD_FORMAT_DIAG D1 CARD_INIT result=%s", esp_err_to_name(result));
    if (result != ESP_OK) {
        (void)sdmmc_host_deinit_slot(FORMAT_SDMMC_SLOT);
        heap_caps_free(raw_card);
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 FAIL stage=card-init result=%s", esp_err_to_name(result));
        return;
    }
    ESP_LOGI(
        TAG,
        "P4_SD_FORMAT_DIAG D1 CARD capacity_bytes=%" PRIu64 " sector_bytes=%d real_khz=%d",
        (uint64_t)raw_card->csd.capacity * (uint64_t)raw_card->csd.sector_size,
        raw_card->csd.sector_size,
        raw_card->real_freq_khz
    );

    uint8_t filesystem_type = 0U;
    ESP_LOGW(TAG, "P4_SD_FORMAT_DIAG D1 FORMAT_BEGIN partition=single-full-card filesystem=FAT32");
    result = force_partition_and_fat32(raw_card, &filesystem_type);
    const esp_err_t raw_cleanup_result = sdmmc_host_deinit_slot(FORMAT_SDMMC_SLOT);
    heap_caps_free(raw_card);
    raw_card = NULL;
    ESP_LOGI(TAG, "P4_SD_FORMAT_DIAG D1 FORMAT_RESULT result=%s fs_type=%u raw_cleanup=%s",
             esp_err_to_name(result), (unsigned)filesystem_type, esp_err_to_name(raw_cleanup_result));
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 FAIL stage=format result=%s", esp_err_to_name(result));
        return;
    }
    if (raw_cleanup_result != ESP_OK) {
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 FAIL stage=raw-cleanup result=%s",
                 esp_err_to_name(raw_cleanup_result));
        return;
    }

    sdmmc_card_t *card = NULL;
    result = mount_card(false, &card);
    ESP_LOGI(TAG, "P4_SD_FORMAT_DIAG D1 INITIAL_MOUNT result=%s", esp_err_to_name(result));
    if (result != ESP_OK || card == NULL) {
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 FAIL stage=initial-mount result=%s",
                 esp_err_to_name(result));
        return;
    }
    result = verify_file_absent();
    if (result != ESP_OK) {
        (void)esp_vfs_fat_sdcard_unmount(FORMAT_MOUNT_POINT, card);
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 FAIL stage=post-format-verify-file-not-absent");
        return;
    }

    char expected_sha256[FORMAT_SHA256_HEX_LENGTH + 1U];
    result = verify_write_read_remove(expected_sha256);
    if (result != ESP_OK) {
        (void)esp_vfs_fat_sdcard_unmount(FORMAT_MOUNT_POINT, card);
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 FAIL stage=mounted-write-read-remove");
        return;
    }
    ESP_LOGI(TAG, "P4_SD_FORMAT_DIAG D1 VERIFY_PASS phase=post-format bytes=%u sha256=%s removed=true",
             FORMAT_VERIFY_BYTES, expected_sha256);

    result = esp_vfs_fat_sdcard_unmount(FORMAT_MOUNT_POINT, card);
    ESP_LOGI(TAG, "P4_SD_FORMAT_DIAG D1 UNMOUNT_RESULT result=%s", esp_err_to_name(result));
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 FAIL stage=unmount result=%s", esp_err_to_name(result));
        return;
    }

    card = NULL;
    result = mount_card(false, &card);
    ESP_LOGI(TAG, "P4_SD_FORMAT_DIAG D1 REMOUNT_RESULT result=%s", esp_err_to_name(result));
    if (result != ESP_OK || card == NULL) {
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 FAIL stage=remount result=%s", esp_err_to_name(result));
        return;
    }
    result = verify_file_absent();
    if (result != ESP_OK) {
        (void)esp_vfs_fat_sdcard_unmount(FORMAT_MOUNT_POINT, card);
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 FAIL stage=post-remount-remove-persistence");
        return;
    }
    ESP_LOGI(TAG, "P4_SD_FORMAT_DIAG D1 REMOVE_PERSISTENCE_PASS absent_after_remount=true");
    result = verify_write_read_remove(expected_sha256);
    if (result != ESP_OK) {
        (void)esp_vfs_fat_sdcard_unmount(FORMAT_MOUNT_POINT, card);
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 FAIL stage=remounted-write-read-remove");
        return;
    }
    ESP_LOGI(TAG, "P4_SD_FORMAT_DIAG D1 VERIFY_PASS phase=post-remount bytes=%u sha256=%s removed=true",
             FORMAT_VERIFY_BYTES, expected_sha256);

    result = esp_vfs_fat_sdcard_unmount(FORMAT_MOUNT_POINT, card);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "P4_SD_FORMAT_DIAG D1 FAIL stage=final-unmount result=%s", esp_err_to_name(result));
        return;
    }
    ESP_LOGI(
        TAG,
        "P4_SD_FORMAT_DIAG D1 COMPLETE filesystem=FAT32 remount=pass write_read=pass"
        " verify_file_removed=true target=sdmmc0-j5 width=1 requested_khz=1000"
    );
#endif
}
