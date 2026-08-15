// SPDX-License-Identifier: MIT

#include "msc_write_policy.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int s_failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        ++s_failures; \
    } \
} while (0)

static int validate(uint8_t lun, uint32_t lba, uint32_t offset,
                    size_t bytes, uint64_t capacity, size_t *address)
{
    return msc_write_policy_validate(
        lun, lba, offset, bytes, 512U, capacity, 8192U, address);
}

int main(void)
{
    const uint64_t capacity = UINT64_C(9289728);
    size_t address = SIZE_MAX;
    CHECK(validate(0U, 1171U, 0U, 8192U, capacity, &address) == 0);
    CHECK(address == 599552U);
    CHECK(validate(0U, 18143U, 0U, 512U, capacity, &address) == 0);
    CHECK(address == 9289216U);

    CHECK(validate(1U, 0U, 0U, 512U, capacity, &address) == EINVAL);
    CHECK(validate(0U, 0U, 1U, 512U, capacity, &address) == EINVAL);
    CHECK(validate(0U, 0U, 0U, 513U, capacity, &address) == EINVAL);
    CHECK(validate(0U, 0U, 0U, 0U, capacity, &address) == EINVAL);
    CHECK(validate(0U, 0U, 0U, 8704U, capacity, &address) == EINVAL);
    CHECK(validate(0U, 18144U, 0U, 512U, capacity, &address) == EOVERFLOW);
    CHECK(validate(0U, UINT32_MAX, 0U, 512U, capacity, &address) ==
          EOVERFLOW);
    CHECK(msc_write_policy_validate(
              0U, 0U, 0U, 512U, 0U, capacity, 8192U, &address) == EINVAL);
    CHECK(validate(0U, 0U, 0U, 512U, capacity, NULL) == EINVAL);

    if (s_failures != 0) {
        fprintf(stderr, "%d MSC write policy test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("platform game storage MSC write policy tests passed");
    return EXIT_SUCCESS;
}
