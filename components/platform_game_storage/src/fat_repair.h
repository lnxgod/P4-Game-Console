// SPDX-License-Identifier: MIT

#ifndef P4_FAT_REPAIR_H
#define P4_FAT_REPAIR_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "sdmmc_cmd.h"

typedef enum {
    P4_FAT_REPAIR_NOT_RUN = 0,
    P4_FAT_REPAIR_CLEAN,
    P4_FAT_REPAIR_REPAIRED,
    P4_FAT_REPAIR_NEEDS_HOST,
    P4_FAT_REPAIR_UNSUPPORTED,
    P4_FAT_REPAIR_FAILED,
} p4_fat_repair_outcome_t;

typedef struct {
    p4_fat_repair_outcome_t outcome;
    uint32_t volume_lba;
    uint32_t total_sectors;
    uint32_t cluster_count;
    uint32_t free_clusters;
    uint32_t fat_mismatch_sectors;
    uint32_t invalid_entries[2];
    uint32_t sectors_rewritten;
    bool primary_boot_restored;
    bool backup_boot_restored;
    bool fat_mirror_restored;
    bool fsinfo_restored;
} p4_fat_repair_report_t;

/**
 * Conservatively repair one unmounted 512-byte-sector FAT32 volume.
 *
 * The routine can restore one valid boot-sector copy, replace a structurally
 * invalid FAT mirror from a structurally valid mirror, and rebuild FAT32
 * FSInfo hints. Ambiguous but structurally valid FAT divergence is rejected;
 * directories, cluster ownership, lost chains, and cross-links are never
 * guessed or modified.
 */
esp_err_t p4_fat_repair_run(sdmmc_card_t *card,
                            p4_fat_repair_report_t *out_report);

#endif
