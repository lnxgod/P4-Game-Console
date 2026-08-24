// SPDX-License-Identifier: MIT

#include "fat_repair.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_heap_caps.h"

enum {
    FAT_SECTOR_BYTES = 512,
    MBR_PARTITION_TABLE_OFFSET = 446,
    MBR_PARTITION_BYTES = 16,
    MBR_PARTITION_COUNT = 4,
    FAT32_MIN_CLUSTERS = 65525,
    FAT32_BAD_CLUSTER = 0x0ffffff7,
    FAT32_EOC_MIN = 0x0ffffff8,
};

typedef struct {
    uint32_t volume_lba;
    uint32_t volume_limit_sectors;
    uint32_t total_sectors;
    uint32_t reserved_sectors;
    uint32_t sectors_per_fat;
    uint32_t data_clusters;
    uint32_t root_cluster;
    uint16_t fsinfo_sector;
    uint16_t backup_boot_sector;
    uint16_t ext_flags;
    uint8_t sectors_per_cluster;
    uint8_t fat_count;
    uint8_t media;
} fat32_layout_t;

static uint16_t load_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8U);
}

static uint32_t load_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8U) |
        ((uint32_t)bytes[2] << 16U) |
        ((uint32_t)bytes[3] << 24U);
}

static void store_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static bool is_power_of_two(uint8_t value)
{
    return value != 0U && (value & (uint8_t)(value - 1U)) == 0U;
}

static bool fat32_partition_type(uint8_t type)
{
    return type == 0x0bU || type == 0x0cU ||
        type == 0x1bU || type == 0x1cU;
}

static esp_err_t read_sector(sdmmc_card_t *card, uint8_t *buffer,
                             uint32_t lba)
{
    return sdmmc_read_sectors(card, buffer, lba, 1U);
}

static esp_err_t write_sector_verified(sdmmc_card_t *card,
                                       const uint8_t *source,
                                       uint8_t *verify,
                                       uint32_t lba)
{
    esp_err_t result = sdmmc_write_sectors(card, source, lba, 1U);
    if (result == ESP_OK) {
        result = read_sector(card, verify, lba);
    }
    if (result == ESP_OK &&
        memcmp(source, verify, FAT_SECTOR_BYTES) != 0) {
        result = ESP_ERR_INVALID_CRC;
    }
    return result;
}

static bool parse_fat32_boot(const uint8_t *sector,
                             uint32_t volume_lba,
                             uint32_t volume_limit_sectors,
                             uint64_t media_sectors,
                             fat32_layout_t *out)
{
    if (sector == NULL || out == NULL ||
        sector[510] != 0x55U || sector[511] != 0xaaU ||
        load_u16(&sector[11]) != FAT_SECTOR_BYTES ||
        !is_power_of_two(sector[13])) {
        return false;
    }
    const uint32_t reserved = load_u16(&sector[14]);
    const uint8_t fats = sector[16];
    const uint32_t total16 = load_u16(&sector[19]);
    const uint32_t total = total16 != 0U
        ? total16 : load_u32(&sector[32]);
    const uint32_t fat16 = load_u16(&sector[22]);
    const uint32_t fat_sectors = load_u32(&sector[36]);
    const uint32_t root_cluster = load_u32(&sector[44]);
    const uint16_t fsinfo = load_u16(&sector[48]);
    const uint16_t backup = load_u16(&sector[50]);
    if (reserved < 2U || fats == 0U || fats > 2U || total == 0U ||
        fat16 != 0U || fat_sectors == 0U ||
        load_u16(&sector[17]) != 0U ||
        load_u16(&sector[42]) != 0U ||
        root_cluster < 2U || fsinfo == 0U || fsinfo >= reserved ||
        backup == 0U || backup >= reserved ||
        (uint64_t)volume_lba + total > media_sectors ||
        (volume_limit_sectors != 0U && total > volume_limit_sectors)) {
        return false;
    }
    const uint64_t metadata = (uint64_t)reserved +
        (uint64_t)fats * fat_sectors;
    if (metadata >= total) {
        return false;
    }
    const uint32_t clusters = (uint32_t)(
        ((uint64_t)total - metadata) / sector[13]);
    const uint64_t fat_entries =
        (uint64_t)fat_sectors * FAT_SECTOR_BYTES / 4U;
    if (clusters < FAT32_MIN_CLUSTERS ||
        fat_entries < (uint64_t)clusters + 2U ||
        root_cluster > clusters + 1U) {
        return false;
    }
    *out = (fat32_layout_t){
        .volume_lba = volume_lba,
        .volume_limit_sectors = volume_limit_sectors,
        .total_sectors = total,
        .reserved_sectors = reserved,
        .sectors_per_fat = fat_sectors,
        .data_clusters = clusters,
        .root_cluster = root_cluster,
        .fsinfo_sector = fsinfo,
        .backup_boot_sector = backup,
        .ext_flags = load_u16(&sector[40]),
        .sectors_per_cluster = sector[13],
        .fat_count = fats,
        .media = sector[21],
    };
    return true;
}

static bool critical_boot_fields_match(const uint8_t *left,
                                       const uint8_t *right)
{
    return memcmp(&left[11], &right[11], 79U) == 0 &&
        left[510] == right[510] && left[511] == right[511];
}

static bool fat_entry_valid(uint32_t index, uint32_t value,
                            uint32_t last_cluster, uint8_t media)
{
    value &= UINT32_C(0x0fffffff);
    if (index == 0U) {
        return (value & UINT32_C(0xff)) == media &&
            (value & UINT32_C(0x0fffff00)) == UINT32_C(0x0fffff00);
    }
    if (index == 1U) {
        return value >= FAT32_EOC_MIN;
    }
    return value == 0U || value == FAT32_BAD_CLUSTER ||
        value >= FAT32_EOC_MIN ||
        (value >= 2U && value <= last_cluster);
}

static esp_err_t scan_fats(sdmmc_card_t *card,
                           const fat32_layout_t *layout,
                           uint8_t *first, uint8_t *second,
                           p4_fat_repair_report_t *report,
                           uint32_t free_clusters[2],
                           uint32_t first_free[2])
{
    const uint32_t fat0 = layout->volume_lba + layout->reserved_sectors;
    const uint32_t entries = layout->data_clusters + 2U;
    for (uint32_t sector = 0U; sector < layout->sectors_per_fat;
         ++sector) {
        esp_err_t result = read_sector(card, first, fat0 + sector);
        if (result != ESP_OK) {
            return result;
        }
        if (layout->fat_count > 1U) {
            result = read_sector(
                card, second, fat0 + layout->sectors_per_fat + sector);
            if (result != ESP_OK) {
                return result;
            }
            if (memcmp(first, second, FAT_SECTOR_BYTES) != 0 &&
                report->fat_mismatch_sectors != UINT32_MAX) {
                ++report->fat_mismatch_sectors;
            }
        }
        const uint32_t first_index = sector * (FAT_SECTOR_BYTES / 4U);
        for (uint32_t offset = 0U;
             offset < FAT_SECTOR_BYTES / 4U &&
             first_index + offset < entries;
             ++offset) {
            const uint32_t index = first_index + offset;
            const uint32_t values[2] = {
                load_u32(&first[offset * 4U]) & UINT32_C(0x0fffffff),
                layout->fat_count > 1U
                    ? load_u32(&second[offset * 4U]) &
                        UINT32_C(0x0fffffff)
                    : 0U,
            };
            for (uint8_t copy = 0U; copy < layout->fat_count; ++copy) {
                if (!fat_entry_valid(index, values[copy],
                                     layout->data_clusters + 1U,
                                     layout->media)) {
                    if (report->invalid_entries[copy] != UINT32_MAX) {
                        ++report->invalid_entries[copy];
                    }
                } else if (index >= 2U && values[copy] == 0U) {
                    if (free_clusters[copy] != UINT32_MAX) {
                        ++free_clusters[copy];
                    }
                    if (first_free[copy] == UINT32_MAX) {
                        first_free[copy] = index;
                    }
                }
            }
        }
    }
    return ESP_OK;
}

static esp_err_t copy_fat_mirror(sdmmc_card_t *card,
                                 const fat32_layout_t *layout,
                                 uint8_t authoritative,
                                 uint8_t *source, uint8_t *other,
                                 uint8_t *verify,
                                 p4_fat_repair_report_t *report)
{
    const uint8_t target = authoritative == 0U ? 1U : 0U;
    const uint32_t fat0 = layout->volume_lba + layout->reserved_sectors;
    const uint32_t source_lba = fat0 +
        (uint32_t)authoritative * layout->sectors_per_fat;
    const uint32_t target_lba = fat0 +
        (uint32_t)target * layout->sectors_per_fat;
    for (uint32_t sector = 0U; sector < layout->sectors_per_fat;
         ++sector) {
        esp_err_t result = read_sector(card, source, source_lba + sector);
        if (result == ESP_OK) {
            result = read_sector(card, other, target_lba + sector);
        }
        if (result != ESP_OK) {
            return result;
        }
        if (memcmp(source, other, FAT_SECTOR_BYTES) == 0) {
            continue;
        }
        result = write_sector_verified(
            card, source, verify, target_lba + sector);
        if (result != ESP_OK) {
            return result;
        }
        if (report->sectors_rewritten != UINT32_MAX) {
            ++report->sectors_rewritten;
        }
    }
    report->fat_mirror_restored = true;
    return ESP_OK;
}

static bool fsinfo_matches(const uint8_t *sector,
                           uint32_t free_clusters,
                           uint32_t next_free)
{
    return load_u32(&sector[0]) == UINT32_C(0x41615252) &&
        load_u32(&sector[484]) == UINT32_C(0x61417272) &&
        load_u32(&sector[488]) == free_clusters &&
        load_u32(&sector[492]) == next_free &&
        sector[510] == 0x55U && sector[511] == 0xaaU;
}

static void build_fsinfo(uint8_t *sector, uint32_t free_clusters,
                         uint32_t next_free)
{
    memset(sector, 0, FAT_SECTOR_BYTES);
    store_u32(&sector[0], UINT32_C(0x41615252));
    store_u32(&sector[484], UINT32_C(0x61417272));
    store_u32(&sector[488], free_clusters);
    store_u32(&sector[492], next_free);
    sector[510] = 0x55U;
    sector[511] = 0xaaU;
}

static esp_err_t repair_fsinfo(sdmmc_card_t *card,
                               const fat32_layout_t *layout,
                               uint32_t free_clusters,
                               uint32_t next_free,
                               uint8_t *sector, uint8_t *replacement,
                               uint8_t *verify,
                               p4_fat_repair_report_t *report)
{
    const uint32_t primary = layout->volume_lba + layout->fsinfo_sector;
    const uint32_t backup = layout->volume_lba +
        layout->backup_boot_sector + layout->fsinfo_sector;
    build_fsinfo(replacement, free_clusters, next_free);
    const uint32_t locations[2] = {backup, primary};
    for (size_t index = 0U; index < 2U; ++index) {
        if (locations[index] >=
            layout->volume_lba + layout->reserved_sectors) {
            continue;
        }
        esp_err_t result = read_sector(card, sector, locations[index]);
        if (result != ESP_OK) {
            return result;
        }
        if (fsinfo_matches(sector, free_clusters, next_free)) {
            continue;
        }
        result = write_sector_verified(
            card, replacement, verify, locations[index]);
        if (result != ESP_OK) {
            return result;
        }
        if (report->sectors_rewritten != UINT32_MAX) {
            ++report->sectors_rewritten;
        }
        report->fsinfo_restored = true;
    }
    return ESP_OK;
}

esp_err_t p4_fat_repair_run(sdmmc_card_t *card,
                            p4_fat_repair_report_t *out_report)
{
    if (card == NULL || out_report == NULL ||
        card->csd.sector_size != FAT_SECTOR_BYTES ||
        card->csd.capacity == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_report, 0, sizeof(*out_report));
    out_report->outcome = P4_FAT_REPAIR_FAILED;
    uint8_t *const workspace = heap_caps_malloc(
        FAT_SECTOR_BYTES * 4U,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (workspace == NULL) {
        return ESP_ERR_NO_MEM;
    }
    uint8_t *const primary = workspace;
    uint8_t *const backup = workspace + FAT_SECTOR_BYTES;
    uint8_t *const scratch = workspace + FAT_SECTOR_BYTES * 2U;
    uint8_t *const verify = workspace + FAT_SECTOR_BYTES * 3U;

    esp_err_t result = read_sector(card, primary, 0U);
    fat32_layout_t layout;
    bool primary_valid = result == ESP_OK && parse_fat32_boot(
        primary, 0U, 0U, card->csd.capacity, &layout);
    uint32_t volume_lba = 0U;
    uint32_t volume_limit = 0U;
    if (!primary_valid && result == ESP_OK &&
        primary[510] == 0x55U && primary[511] == 0xaaU) {
        for (size_t index = 0U; index < MBR_PARTITION_COUNT; ++index) {
            const uint8_t *const entry = &primary[
                MBR_PARTITION_TABLE_OFFSET + index * MBR_PARTITION_BYTES];
            if (!fat32_partition_type(entry[4])) {
                continue;
            }
            const uint32_t start = load_u32(&entry[8]);
            const uint32_t count = load_u32(&entry[12]);
            if (start == 0U || count == 0U ||
                (uint64_t)start + count > card->csd.capacity) {
                continue;
            }
            result = read_sector(card, primary, start);
            if (result != ESP_OK) {
                break;
            }
            volume_lba = start;
            volume_limit = count;
            primary_valid = parse_fat32_boot(
                primary, start, count, card->csd.capacity, &layout);
            break;
        }
    }
    if (result != ESP_OK) {
        goto finish;
    }

    bool restore_primary = false;
    if (!primary_valid) {
        const uint32_t candidate_backup = volume_lba + 6U;
        result = read_sector(card, backup, candidate_backup);
        if (result != ESP_OK || !parse_fat32_boot(
                backup, volume_lba, volume_limit, card->csd.capacity,
                &layout) || layout.backup_boot_sector != 6U) {
            out_report->outcome = P4_FAT_REPAIR_UNSUPPORTED;
            result = ESP_ERR_NOT_SUPPORTED;
            goto finish;
        }
        memcpy(primary, backup, FAT_SECTOR_BYTES);
        restore_primary = true;
    }
    out_report->volume_lba = layout.volume_lba;
    out_report->total_sectors = layout.total_sectors;
    out_report->cluster_count = layout.data_clusters;

    const uint32_t backup_lba =
        layout.volume_lba + layout.backup_boot_sector;
    result = read_sector(card, backup, backup_lba);
    fat32_layout_t backup_layout;
    const bool backup_valid = result == ESP_OK && parse_fat32_boot(
        backup, layout.volume_lba, layout.volume_limit_sectors,
        card->csd.capacity, &backup_layout);
    if (result != ESP_OK) {
        goto finish;
    }
    if (backup_valid && !critical_boot_fields_match(primary, backup)) {
        out_report->outcome = P4_FAT_REPAIR_NEEDS_HOST;
        result = ESP_ERR_INVALID_CRC;
        goto finish;
    }

    uint32_t free_clusters[2] = {0U, 0U};
    uint32_t first_free[2] = {UINT32_MAX, UINT32_MAX};
    result = scan_fats(card, &layout, backup, scratch, out_report,
                       free_clusters, first_free);
    if (result != ESP_OK) {
        goto finish;
    }

    uint8_t authoritative = 0U;
    bool copy_mirror = false;
    if (layout.fat_count == 1U) {
        if (out_report->invalid_entries[0] != 0U) {
            out_report->outcome = P4_FAT_REPAIR_NEEDS_HOST;
            result = ESP_ERR_INVALID_CRC;
            goto finish;
        }
    } else if (out_report->fat_mismatch_sectors == 0U) {
        if (out_report->invalid_entries[0] != 0U ||
            out_report->invalid_entries[1] != 0U) {
            out_report->outcome = P4_FAT_REPAIR_NEEDS_HOST;
            result = ESP_ERR_INVALID_CRC;
            goto finish;
        }
    } else if ((layout.ext_flags & UINT16_C(0x0080)) != 0U) {
        authoritative = (uint8_t)(layout.ext_flags & UINT16_C(0x000f));
        if (authoritative >= layout.fat_count ||
            out_report->invalid_entries[authoritative] != 0U) {
            out_report->outcome = P4_FAT_REPAIR_NEEDS_HOST;
            result = ESP_ERR_INVALID_CRC;
            goto finish;
        }
        copy_mirror = true;
    } else if (out_report->invalid_entries[0] == 0U &&
               out_report->invalid_entries[1] > 0U) {
        authoritative = 0U;
        copy_mirror = true;
    } else if (out_report->invalid_entries[1] == 0U &&
               out_report->invalid_entries[0] > 0U) {
        authoritative = 1U;
        copy_mirror = true;
    } else {
        /* Two plausible but different allocation histories require a full
         * directory-aware fsck. Never guess which one owns a child's data. */
        out_report->outcome = P4_FAT_REPAIR_NEEDS_HOST;
        result = ESP_ERR_INVALID_CRC;
        goto finish;
    }

    if (restore_primary) {
        result = write_sector_verified(
            card, primary, verify, layout.volume_lba);
        if (result != ESP_OK) {
            goto finish;
        }
        ++out_report->sectors_rewritten;
        out_report->primary_boot_restored = true;
    } else if (!backup_valid) {
        result = write_sector_verified(card, primary, verify, backup_lba);
        if (result != ESP_OK) {
            goto finish;
        }
        ++out_report->sectors_rewritten;
        out_report->backup_boot_restored = true;
    }
    if (copy_mirror) {
        result = copy_fat_mirror(card, &layout, authoritative,
                                 backup, scratch, verify, out_report);
        if (result != ESP_OK) {
            goto finish;
        }
    }
    out_report->free_clusters = free_clusters[authoritative];
    const uint32_t next_free = first_free[authoritative] == UINT32_MAX
        ? UINT32_C(0xffffffff) : first_free[authoritative];
    result = repair_fsinfo(card, &layout, out_report->free_clusters,
                           next_free, backup, scratch, verify, out_report);
    if (result == ESP_OK) {
        result = sdmmc_get_status(card);
    }
    if (result == ESP_OK) {
        out_report->outcome = out_report->sectors_rewritten == 0U
            ? P4_FAT_REPAIR_CLEAN : P4_FAT_REPAIR_REPAIRED;
    }

finish:
    if (result != ESP_OK &&
        out_report->outcome == P4_FAT_REPAIR_FAILED) {
        out_report->outcome = P4_FAT_REPAIR_FAILED;
    }
    heap_caps_free(workspace);
    return result;
}
