// SPDX-License-Identifier: MIT

#ifndef P4_TEST_MBEDTLS_SHA256_H
#define P4_TEST_MBEDTLS_SHA256_H

#include <stddef.h>

int mbedtls_sha256(const unsigned char *input, size_t input_length,
                   unsigned char output[32], int is224);

#endif
