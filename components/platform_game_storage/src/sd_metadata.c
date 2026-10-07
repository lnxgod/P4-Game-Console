// SPDX-License-Identifier: MIT
#include "sd_metadata.h"

#include <string.h>

static void append_be32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

bool p4_sd_metadata_cid_hash(const p4_sd_cid_t *cid,
                            p4_sd_metadata_sha256_fn sha256,
                            char out_hex[65])
{
    if (out_hex == NULL) return false;
    memset(out_hex, 0, 65U);
    if (cid == NULL || sha256 == NULL || cid->manufacturer > UINT8_MAX ||
        cid->oem > UINT16_MAX || cid->revision > UINT8_MAX ||
        cid->date > UINT32_C(0x0fff)) return false;

    uint8_t material[33] = {'P', '4', 'S', 'D', 'C', 'I', 'D', '1'};
    append_be32(material + 8U, cid->manufacturer);
    append_be32(material + 12U, cid->oem);
    memcpy(material + 16U, cid->product_name, sizeof(cid->product_name));
    append_be32(material + 21U, cid->revision);
    append_be32(material + 25U, cid->serial);
    append_be32(material + 29U, cid->date);
    uint8_t digest[32] = {0};
    if (!sha256(material, sizeof(material), digest)) return false;
    static const char hex[] = "0123456789abcdef";
    for (size_t index = 0U; index < sizeof(digest); ++index) {
        out_hex[index * 2U] = hex[digest[index] >> 4U];
        out_hex[index * 2U + 1U] = hex[digest[index] & UINT8_C(0x0f)];
    }
    return true;
}

unsigned p4_sd_metadata_ssr_width(uint32_t encoded_width)
{
    return encoded_width == 0U ? 1U : encoded_width == 2U ? 4U : 0U;
}

static bool power_of_two(uint32_t value)
{
    return value != 0U && (value & (value - 1U)) == 0U;
}

bool p4_sd_metadata_fat_geometry(const p4_sd_fat_input_t *input,
                                p4_sd_fat_geometry_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    if (input == NULL || input->fat_type < 1U || input->fat_type > 4U ||
        input->sector_bytes < 512U || input->sector_bytes > 4096U ||
        !power_of_two(input->sector_bytes) ||
        input->sectors_per_cluster > UINT16_MAX ||
        !power_of_two(input->sectors_per_cluster) ||
        input->fat_entries < 3U ||
        input->free_clusters > input->fat_entries - 2U) return false;

    const uint32_t clusters = input->fat_entries - 2U;
    const uint64_t data_sectors =
        (uint64_t)clusters * input->sectors_per_cluster;
    const uint64_t physical_sectors =
        input->physical_bytes / input->sector_bytes;
    if (input->physical_bytes % input->sector_bytes != 0U ||
        input->volume_lba >= input->data_lba ||
        input->data_lba >= physical_sectors ||
        data_sectors > physical_sectors - input->data_lba) return false;

    /* Bounds above give at most 4096 * 32768 bytes per cluster and fewer
     * than 2^32 clusters, so neither product can overflow its destination. */
    out->data_clusters = clusters;
    out->cluster_bytes = input->sector_bytes * input->sectors_per_cluster;
    out->data_bytes = (uint64_t)clusters * out->cluster_bytes;
    out->free_bytes = (uint64_t)input->free_clusters * out->cluster_bytes;
    return true;
}
