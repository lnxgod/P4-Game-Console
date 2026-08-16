// SPDX-License-Identifier: MIT

#include "p4/content_catalog.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: p4_content_inspect <storage-root>\n");
        return EXIT_FAILURE;
    }
    p4_content_catalog_t catalog;
    const p4_content_status_t status = p4_content_catalog_scan(argv[1], &catalog);
    if (status != P4_CONTENT_OK) {
        fprintf(stderr, "P4_CONTENT_SCAN FAIL status=%s\n",
                p4_content_status_name(status));
        return EXIT_FAILURE;
    }
    printf("P4_CONTENT_SCAN PASS storage=%u candidates=%u carts=%u invalid=%u "
           "truncated=%u\n",
           catalog.storage_available ? 1U : 0U,
           (unsigned)catalog.candidates_seen,
           (unsigned)catalog.valid_cart_count,
           (unsigned)catalog.invalid_cart_count,
           catalog.directory_truncated ? 1U : 0U);
    for (size_t index = 0U; index < catalog.valid_cart_count; ++index) {
        char digest[65];
        p4_content_sha256_hex(catalog.carts[index].sha256, digest);
        printf("P4_CONTENT_CART name=%s bytes=%llu sha256=%s\n",
               catalog.carts[index].name,
               (unsigned long long)catalog.carts[index].size_bytes,
               digest);
    }
    return EXIT_SUCCESS;
}
