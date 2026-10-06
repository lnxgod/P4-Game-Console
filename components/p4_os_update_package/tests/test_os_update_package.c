// SPDX-License-Identifier: MIT

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p4/os_update_package.h"

static void write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static void make_valid(uint8_t package[P4_OS_UPDATE_HEADER_BYTES + 4U])
{
    memset(package, 0, P4_OS_UPDATE_HEADER_BYTES + 4U);
    memcpy(package, P4_OS_UPDATE_MAGIC, 8U);
    write_u32(package + 8U, P4_OS_UPDATE_HEADER_BYTES);
    write_u32(package + 12U, P4_OS_UPDATE_HEADER_BYTES + 4U);
    write_u32(package + 16U, P4_OS_UPDATE_HEADER_BYTES);
    write_u32(package + 20U, 4U);
    write_u32(package + 24U, 1U);
    memcpy(package + 64U, "1.2.3", sizeof("1.2.3"));
    memcpy(package + 96U, "test-build", sizeof("test-build"));
    memcpy(package + 160U, P4_OS_UPDATE_ACCEPTED_TARGET, sizeof(P4_OS_UPDATE_ACCEPTED_TARGET));
    package[P4_OS_UPDATE_HEADER_BYTES] = UINT8_C(0xe9);
}

int main(void)
{
    uint8_t package[P4_OS_UPDATE_HEADER_BYTES + 4U];
    make_valid(package);
    p4_os_update_package_info_t info;
    assert(p4_os_update_package_parse(package, sizeof(package), &info) ==
           P4_OS_UPDATE_PACKAGE_VALID);
    assert(info.payload_bytes == 4U);
    assert(strcmp(info.version, "1.2.3") == 0);
    assert(strcmp(info.build, "test-build") == 0);
    assert(strcmp(info.target, P4_OS_UPDATE_ACCEPTED_TARGET) == 0);

    assert(p4_os_update_package_parse(NULL, sizeof(package), &info) ==
           P4_OS_UPDATE_PACKAGE_BAD_ARGUMENT);
    assert(p4_os_update_package_parse(package, 12U, &info) ==
           P4_OS_UPDATE_PACKAGE_BAD_SIZE);
    package[0] ^= UINT8_C(1);
    assert(p4_os_update_package_parse(package, sizeof(package), &info) ==
           P4_OS_UPDATE_PACKAGE_BAD_MAGIC);
    make_valid(package);
    package[28] = 1U;
    assert(p4_os_update_package_parse(package, sizeof(package), &info) ==
           P4_OS_UPDATE_PACKAGE_BAD_VERSION);
    make_valid(package);
    package[176] = 1U;
    assert(p4_os_update_package_parse(package, sizeof(package), &info) ==
           P4_OS_UPDATE_PACKAGE_BAD_LAYOUT);
    make_valid(package);
    package[160] = 'x';
    assert(p4_os_update_package_parse(package, sizeof(package), &info) ==
           P4_OS_UPDATE_PACKAGE_BAD_METADATA);
    make_valid(package);
    const char *other = strcmp(P4_OS_UPDATE_ACCEPTED_TARGET,"esp32p4") == 0
        ? "esp32p4-tab5" : "esp32p4";
    memset(package+160U,0,16U);
    memcpy(package+160U,other,strlen(other));
    assert(p4_os_update_package_parse(package,sizeof(package),&info)==
           P4_OS_UPDATE_PACKAGE_BAD_METADATA);
    make_valid(package);
    package[P4_OS_UPDATE_HEADER_BYTES] = 0U;
    assert(p4_os_update_package_parse(package, sizeof(package), &info) ==
           P4_OS_UPDATE_PACKAGE_BAD_IMAGE);
    assert(strcmp(p4_os_update_package_result_name(
                      P4_OS_UPDATE_PACKAGE_BAD_LAYOUT),
                  "bad-layout") == 0);
    /* Exercise each board's real OTA boundary and one byte beyond it. */
    const size_t limit = P4_OS_UPDATE_MAX_PACKAGE_BYTES;
    uint8_t *large = calloc(limit + 1U, 1U);
    assert(large != NULL);
    make_valid(large);
    write_u32(large + 12U, (uint32_t)limit);
    write_u32(large + 20U, P4_OS_UPDATE_MAX_IMAGE_BYTES);
    assert(p4_os_update_package_parse(large, limit, &info) == P4_OS_UPDATE_PACKAGE_VALID);
    assert(p4_os_update_package_parse(large, limit + 1U, &info) == P4_OS_UPDATE_PACKAGE_BAD_SIZE);
    free(large);
    puts("p4 OS update package tests passed");
    return 0;
}
