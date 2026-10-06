// SPDX-License-Identifier: MIT

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p4/game.h"
#include "p4/game_package.h"
#include "p4/game_resource.h"

enum {
    SYNTHETIC_ELF_BYTES = 256,
    SYNTHETIC_PACKAGE_BYTES =
        P4_GAME_PACKAGE_HEADER_BYTES + SYNTHETIC_ELF_BYTES,
};

static void write_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static void make_synthetic(uint8_t package[SYNTHETIC_PACKAGE_BYTES])
{
    memset(package, 0, SYNTHETIC_PACKAGE_BYTES);
    memcpy(package, P4_GAME_PACKAGE_MAGIC, 8U);
    write_u32(package + 8U, P4_GAME_PACKAGE_HEADER_BYTES);
    write_u32(package + 12U, SYNTHETIC_PACKAGE_BYTES);
    write_u32(package + 16U, P4_GAME_PACKAGE_HEADER_BYTES);
    write_u32(package + 20U, SYNTHETIC_ELF_BYTES);
    write_u32(package + 24U, P4_GAME_PACKAGE_FORMAT_VERSION);
    write_u32(package + 28U, P4_GAME_PACKAGE_API_VERSION);
    write_u32(package + 32U, 100U);
    write_u32(package + 36U, UINT32_C(3));
    write_u16(package + 44U, UINT16_C(0x5fea));
    write_u16(package + 46U, P4_GAME_PACKAGE_FLAG_DEVELOPMENT);
    memcpy(package + 80U, "org.test.synthetic", sizeof("org.test.synthetic"));
    memcpy(package + 128U, "SYNTHETIC", sizeof("SYNTHETIC"));
    memcpy(package + 144U, "HOST TEST", sizeof("HOST TEST"));
    memcpy(package + 176U, "GAMES/TEST", sizeof("GAMES/TEST"));
    memcpy(package + 208U, "1.0.0", sizeof("1.0.0"));
    memcpy(package + 224U, "MIT", sizeof("MIT"));

    uint8_t *const elf = package + P4_GAME_PACKAGE_HEADER_BYTES;
    memcpy(elf, "\x7f" "ELF", 4U);
    elf[4] = 1U;
    elf[5] = 1U;
    elf[6] = 1U;
    write_u16(elf + 16U, 3U);
    write_u16(elf + 18U, 243U);
    write_u32(elf + 20U, 1U);
    write_u32(elf + 24U, 1U);
    write_u32(elf + 28U, 52U);
    write_u32(elf + 32U, 128U);
    write_u16(elf + 40U, 52U);
    write_u16(elf + 42U, 32U);
    write_u16(elf + 44U, 1U);
    write_u16(elf + 46U, 40U);
    write_u16(elf + 48U, 2U);
    write_u16(elf + 50U, 1U);
    write_u32(elf + 52U, 1U);
    write_u32(elf + 56U, 0U);
    write_u32(elf + 60U, 0U);
    write_u32(elf + 68U, SYNTHETIC_ELF_BYTES);
    write_u32(elf + 72U, SYNTHETIC_ELF_BYTES);
    write_u32(elf + 76U, 1U);
    write_u32(elf + 80U, 4U);
    write_u32(elf + 168U, 1U);
    write_u32(elf + 172U, 3U);
    write_u32(elf + 184U, 220U);
    write_u32(elf + 188U, 11U);
    memcpy(elf + 220U, "\0.shstrtab\0", 11U);
}

static void test_resource_package(void)
{
    uint8_t resource[P4_GAME_RESOURCE_HEADER_BYTES + 16U] = {0};
    memcpy(resource, P4_GAME_RESOURCE_MAGIC, 8U);
    write_u32(resource + 8U, P4_GAME_RESOURCE_HEADER_BYTES);
    write_u32(resource + 12U, (uint32_t)sizeof(resource));
    write_u32(resource + 16U, P4_GAME_RESOURCE_HEADER_BYTES);
    write_u32(resource + 20U, 16U);
    write_u32(resource + 24U, P4_GAME_RESOURCE_FORMAT_VERSION);
    memcpy(resource + 64U, "org.test.synthetic",
           sizeof("org.test.synthetic"));
    for (size_t index = P4_GAME_RESOURCE_HEADER_BYTES;
         index < sizeof(resource); ++index) {
        resource[index] = (uint8_t)index;
    }
    p4_game_resource_info_t info;
    assert(p4_game_resource_parse(resource, sizeof(resource), &info) ==
           P4_GAME_RESOURCE_VALID);
    assert(strcmp(info.game_id, "org.test.synthetic") == 0);
    assert(info.payload_offset == P4_GAME_RESOURCE_HEADER_BYTES);
    assert(info.payload_bytes == 16U);

    resource[0] ^= UINT8_C(1);
    assert(p4_game_resource_parse(resource, sizeof(resource), &info) ==
           P4_GAME_RESOURCE_BAD_MAGIC);
    resource[0] ^= UINT8_C(1);
    resource[112] = UINT8_C(1);
    assert(p4_game_resource_parse(resource, sizeof(resource), &info) ==
           P4_GAME_RESOURCE_BAD_LAYOUT);
    resource[112] = UINT8_C(0);
    resource[64] = 'O';
    assert(p4_game_resource_parse(resource, sizeof(resource), &info) ==
           P4_GAME_RESOURCE_BAD_METADATA);
    assert(strcmp(p4_game_resource_result_name(
                      P4_GAME_RESOURCE_BAD_DIGEST), "bad-digest") == 0);
}

static uint8_t *read_file(const char *path, size_t *out_bytes)
{
    FILE *const file = fopen(path, "rb");
    assert(file != NULL);
    assert(fseek(file, 0L, SEEK_END) == 0);
    const long length = ftell(file);
    assert(length > 0L);
    assert(fseek(file, 0L, SEEK_SET) == 0);
    uint8_t *const data = malloc((size_t)length);
    assert(data != NULL);
    assert(fread(data, 1U, (size_t)length, file) == (size_t)length);
    assert(fclose(file) == 0);
    *out_bytes = (size_t)length;
    return data;
}

static void expect_package(const char *path, const char *expected_id)
{
    size_t bytes = 0U;
    uint8_t *const data = read_file(path, &bytes);
    p4_game_package_info_t info;
    assert(p4_game_package_parse(data, bytes, &info) ==
           P4_GAME_PACKAGE_VALID);
    assert(strcmp(info.id, expected_id) == 0);
    assert(info.payload_offset == P4_GAME_PACKAGE_HEADER_BYTES);
    assert(info.payload_bytes + info.payload_offset == bytes);

    data[0] ^= UINT8_C(1);
    assert(p4_game_package_parse(data, bytes, &info) ==
           P4_GAME_PACKAGE_BAD_MAGIC);
    data[0] ^= UINT8_C(1);
    data[bytes - 1U] ^= UINT8_C(1);
    /* Structural parsing is separate from the payload digest check. */
    assert(p4_game_package_parse(data, bytes, &info) ==
           P4_GAME_PACKAGE_VALID);
    free(data);
}

static void test_cartridge_icon(void)
{
    uint8_t package[SYNTHETIC_PACKAGE_BYTES]; make_synthetic(package);
    uint8_t elf[288 + P4_GAME_ICON_BYTES] = {0};
    memcpy(elf,package+P4_GAME_PACKAGE_HEADER_BYTES,SYNTHETIC_ELF_BYTES);
    write_u16(elf+48,3); write_u32(elf+184,256); write_u32(elf+188,19);
    memcpy(elf+256,"\0.shstrtab\0.p4icon",19);
    write_u32(elf+208,11); write_u32(elf+212,1);
    write_u32(elf+224,288); write_u32(elf+228,P4_GAME_ICON_BYTES);
    memcpy(elf+288,"P4ICON1\0",8); write_u16(elf+296,128); write_u16(elf+298,72); write_u32(elf+300,1);
    write_u16(elf+304,0xf800); memset(elf+288+528,7,P4_GAME_ICON_PIXELS);
    p4_game_icon_t icon;
    assert(p4_game_package_read_icon(elf,sizeof(elf),&icon));
    assert(icon.palette[0]==0xf800 && icon.pixels[P4_GAME_ICON_PIXELS-1]==7);
    write_u16(elf+296,65535);assert(!p4_game_package_read_icon(elf,sizeof(elf),&icon));write_u16(elf+296,128);
    write_u32(elf+224,UINT32_MAX);assert(!p4_game_package_read_icon(elf,sizeof(elf),&icon));write_u32(elf+224,288);
    write_u32(elf+216,2);assert(!p4_game_package_read_icon(elf,sizeof(elf),&icon));write_u32(elf+216,0);
    assert(!p4_game_package_read_icon(elf,sizeof(elf)-1,&icon));
    assert(!p4_game_package_read_icon(package+P4_GAME_PACKAGE_HEADER_BYTES,SYNTHETIC_ELF_BYTES,&icon));
}

int main(int argc, char *argv[])
{
    if (argc==3 && strcmp(argv[1],"--icon")==0) {
        FILE *f=fopen(argv[2],"rb");assert(f);
        assert(fseek(f,0,SEEK_END)==0);long bytes=ftell(f);assert(bytes>0 && bytes<=P4_GAME_PACKAGE_MAX_BYTES);
        assert(fseek(f,0,SEEK_SET)==0);uint8_t *data=malloc((size_t)bytes);assert(data);
        assert(fread(data,1,(size_t)bytes,f)==(size_t)bytes);fclose(f);
        p4_game_icon_t icon;assert(p4_game_package_read_icon(data,(size_t)bytes,&icon));
        free(data);puts("Real RISC-V cartridge icon validated");return 0;
    }
    assert(argc == 1 || argc == 3);
    test_resource_package();
    test_cartridge_icon();
    if (argc == 3) {
        expect_package(argv[1], "org.p4console.maze-chase");
        expect_package(argv[2], "org.p4console.space-invaders");
    }

    uint8_t package[SYNTHETIC_PACKAGE_BYTES];
    make_synthetic(package);
    p4_game_package_info_t info;
    assert(p4_game_package_parse(package, sizeof(package), &info) ==
           P4_GAME_PACKAGE_VALID);
    assert(strcmp(info.id, "org.test.synthetic") == 0);
    write_u32(package + 40U,
              P4_GAME_CAP_SAVE | P4_GAME_CAP_TEXT_INPUT |
              P4_GAME_CAP_REALM | P4_GAME_CAP_MULTIPLAYER_SESSION |
              P4_GAME_CAP_MODULE_HANDOFF | P4_GAME_CAP_VECTOR_SCENES |
              P4_GAME_CAP_VIDEO_HIGH_RES);
    assert(p4_game_package_parse(package, sizeof(package), &info) ==
           P4_GAME_PACKAGE_VALID);
    assert(!info.multiplayer_profile_declared);
    assert(info.multiplayer_profile.style ==
           P4_GAME_MULTIPLAYER_STYLE_REALTIME);
    assert(info.multiplayer_profile.message_bytes ==
           P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES);
    make_synthetic(package);
    write_u32(package + 40U, P4_GAME_CAP_MULTIPLAYER_SESSION);
    write_u16(package + 46U,
              P4_GAME_PACKAGE_FLAG_DEVELOPMENT |
              P4_GAME_PACKAGE_FLAG_MULTIPLAYER_PROFILE);
    memcpy(package + 240U, "P4MP", 4U);
    package[244] = P4_GAME_MULTIPLAYER_PROFILE_SCHEMA;
    package[245] = P4_GAME_MULTIPLAYER_STYLE_TURN_BASED;
    package[246] = 2U;
    package[247] = 2U;
    write_u16(package + 248U, 10U);
    package[250] = 0U;
    package[251] = 48U;
    write_u16(package + 252U, 7U);
    assert(p4_game_package_parse(package, sizeof(package), &info) ==
           P4_GAME_PACKAGE_VALID);
    assert(info.multiplayer_profile_declared);
    assert(info.multiplayer_profile.style ==
           P4_GAME_MULTIPLAYER_STYLE_TURN_BASED);
    assert(info.multiplayer_profile.message_bytes == 48U);
    assert(info.multiplayer_profile.protocol == 7U);
    package[251] = 65U;
    assert(p4_game_package_parse(package, sizeof(package), &info) ==
           P4_GAME_PACKAGE_BAD_METADATA);
    make_synthetic(package);
    write_u16(package + 46U,
              P4_GAME_PACKAGE_FLAG_DEVELOPMENT |
              P4_GAME_PACKAGE_FLAG_MULTIPLAYER_PROFILE);
    memcpy(package + 240U, "P4MP", 4U);
    package[244] = P4_GAME_MULTIPLAYER_PROFILE_SCHEMA;
    package[245] = P4_GAME_MULTIPLAYER_STYLE_REALTIME;
    package[246] = 2U;
    package[247] = 2U;
    write_u16(package + 248U, 30U);
    package[251] = 64U;
    write_u16(package + 252U, 1U);
    assert(p4_game_package_parse(package, sizeof(package), &info) ==
           P4_GAME_PACKAGE_BAD_METADATA);
    write_u32(package + 40U, UINT32_C(1) << 31U);
    assert(p4_game_package_parse(package, sizeof(package), &info) ==
           P4_GAME_PACKAGE_BAD_METADATA);
    make_synthetic(package);
    package[240] = 1U;
    assert(p4_game_package_parse(package, sizeof(package), &info) ==
           P4_GAME_PACKAGE_BAD_LAYOUT);
    make_synthetic(package);
    package[46] = UINT8_C(0x80);
    assert(p4_game_package_parse(package, sizeof(package), &info) ==
           P4_GAME_PACKAGE_BAD_METADATA);
    make_synthetic(package);
    package[P4_GAME_PACKAGE_HEADER_BYTES + 168U] = UINT8_C(0xff);
    assert(p4_game_package_parse(package, sizeof(package), &info) ==
           P4_GAME_PACKAGE_BAD_ELF);

    uint8_t tiny[32] = {0};
    assert(p4_game_package_parse(tiny, sizeof(tiny), &info) ==
           P4_GAME_PACKAGE_BAD_SIZE);
    assert(strcmp(p4_game_package_result_name(P4_GAME_PACKAGE_BAD_ELF),
                  "bad-elf") == 0);
    puts("p4 game package tests passed");
    return 0;
}
