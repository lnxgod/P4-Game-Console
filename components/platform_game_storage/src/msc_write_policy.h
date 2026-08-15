// SPDX-License-Identifier: MIT

#ifndef P4_MSC_WRITE_POLICY_H
#define P4_MSC_WRITE_POLICY_H

#include <stddef.h>
#include <stdint.h>

/**
 * Validate one USB MSC WRITE(10) range and return its byte address.
 *
 * The Console OS exposes one 512-byte-sector LUN. Writes must be complete
 * sectors so the wear-levelling erase and write operations are atomic at the
 * storage sector boundary. Returns zero or a positive errno value.
 */
int msc_write_policy_validate(uint8_t lun, uint32_t lba, uint32_t offset,
                              size_t size_bytes, uint32_t sector_size_bytes,
                              uint64_t capacity_bytes,
                              size_t maximum_transfer_bytes,
                              size_t *out_address);

#endif
