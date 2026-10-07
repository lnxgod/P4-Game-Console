// SPDX-License-Identifier: MIT
#ifndef P4_SD_METADATA_H
#define P4_SD_METADATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* SD memory-card decoded CID only; never hash C structure padding. The five
 * product-name bytes are fixed-width SD CID data, not a C string. */
typedef struct {
    uint32_t manufacturer;
    uint32_t oem;
    uint8_t product_name[5];
    uint32_t revision;
    uint32_t serial;
    uint32_t date;
} p4_sd_cid_t;

typedef bool (*p4_sd_metadata_sha256_fn)(
    const void *data, size_t bytes, uint8_t digest[32]);

/* Schema p4-sd-cid-v1: ASCII P4SDCID1, big-endian uint32 manufacturer and
 * OEM, five product-name bytes, then big-endian uint32 revision, serial and
 * date. Exactly 33 bytes; excludes unavailable CID CRC/reserved bits. */
bool p4_sd_metadata_cid_hash(const p4_sd_cid_t *cid,
                            p4_sd_metadata_sha256_fn sha256,
                            char out_hex[65]);

/* Cached SSR CURRENT_BUS_WIDTH encoding. Zero means unknown on reserved
 * input, not a negotiated zero-bit bus. This does not query the card. */
unsigned p4_sd_metadata_ssr_width(uint32_t encoded_width);

typedef struct {
    uint32_t fat_type; /* pinned FatFs: 1=FAT12, 2=FAT16, 3=FAT32, 4=exFAT */
    uint32_t sector_bytes;
    uint32_t sectors_per_cluster;
    uint32_t fat_entries;
    uint32_t free_clusters;
    uint64_t volume_lba;
    uint64_t data_lba;
    uint64_t physical_bytes;
} p4_sd_fat_input_t;

typedef struct {
    uint32_t data_clusters;
    uint32_t cluster_bytes;
    uint64_t data_bytes;
    uint64_t free_bytes;
} p4_sd_fat_geometry_t;

/* Validates copied, already-mounted FAT geometry. No I/O and no mutation of
 * FatFs state. data_bytes excludes FAT/boot overhead: it is not partition size. */
bool p4_sd_metadata_fat_geometry(const p4_sd_fat_input_t *input,
                                p4_sd_fat_geometry_t *out);

#endif
