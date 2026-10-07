// SPDX-License-Identifier: MIT
#include "verified_reader.h"
#include <assert.h>
#include <stdio.h>

/* Keep the hardware contract independent of the implementation spelling. */
_Static_assert(_Alignof(p4_verified_reader_t) >= 64,
               "SDMMC cache storage requires 64-byte alignment");
_Static_assert(offsetof(p4_verified_reader_t, cache) % 64 == 0,
               "cache member must be aligned independently of array base");
_Static_assert(sizeof(p4_verified_reader_t) % 64 == 0,
               "array stride must preserve every cache alignment");
#ifdef P4_VERIFY_TARGET32
_Static_assert(sizeof(void *) == 4 && sizeof(size_t) == 4,
               "target layout proof must use the 32-bit compiler");
_Static_assert(offsetof(p4_verified_reader_t, cache) == 64,
               "target cache offset changed");
_Static_assert(sizeof(p4_verified_reader_t) == 4224,
               "target reader size changed");
#endif

static p4_verified_reader_t static_readers[3];

/* Also expose the values in the target object for readelf inspection. */
const unsigned p4_verified_reader_layout[] = {
    sizeof(void *), _Alignof(p4_verified_reader_t),
    offsetof(p4_verified_reader_t, cache), sizeof(p4_verified_reader_t)
};

static void check_caches(p4_verified_reader_t readers[3])
{
    for (size_t i = 0; i < 3; ++i) {
        assert((uintptr_t)readers[i].cache % 64U == 0U);
        readers[i].cache[0] = (uint8_t)i;
        readers[i].cache[P4_VERIFIED_BLOCK_BYTES - 1] = (uint8_t)(i + 1U);
    }
}

int main(void)
{
    p4_verified_reader_t stack_readers[3] = {0};
    check_caches(static_readers);
    check_caches(stack_readers);
    printf("verified reader layout: pointer=%zu align=%zu offset=%zu stride=%zu; "
           "all static and stack caches aligned\n", sizeof(void *),
           _Alignof(p4_verified_reader_t),
           offsetof(p4_verified_reader_t, cache), sizeof(p4_verified_reader_t));
    return 0;
}
