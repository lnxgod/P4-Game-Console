// SPDX-License-Identifier: MIT

#include "msc_write_policy.h"

#include <errno.h>
#include <limits.h>

int msc_write_policy_validate(uint8_t lun, uint32_t lba, uint32_t offset,
                              size_t size_bytes, uint32_t sector_size_bytes,
                              uint64_t capacity_bytes,
                              size_t maximum_transfer_bytes,
                              uint64_t *out_address)
{
    if (out_address == NULL || lun != 0U || sector_size_bytes == 0U ||
        size_bytes == 0U || maximum_transfer_bytes == 0U ||
        size_bytes > maximum_transfer_bytes || size_bytes > INT32_MAX ||
        offset >= sector_size_bytes ||
        offset % sector_size_bytes != 0U ||
        size_bytes % sector_size_bytes != 0U) {
        return EINVAL;
    }

    const uint64_t address =
        (uint64_t)lba * (uint64_t)sector_size_bytes + (uint64_t)offset;
    if (address > capacity_bytes ||
        (uint64_t)size_bytes > capacity_bytes - address) {
        return EOVERFLOW;
    }
    *out_address = address;
    return 0;
}
