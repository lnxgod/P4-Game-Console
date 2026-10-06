// SPDX-License-Identifier: MIT

#ifndef P4_GAME_PACKAGE_H
#define P4_GAME_PACKAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/game.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_GAME_PACKAGE_HEADER_BYTES = 256,
    P4_GAME_PACKAGE_SHA256_BYTES = 32,
    P4_GAME_PACKAGE_ID_BYTES = 48,
    P4_GAME_PACKAGE_TITLE_BYTES = 16,
    P4_GAME_PACKAGE_SUBTITLE_BYTES = 32,
    P4_GAME_PACKAGE_FOLDER_BYTES = 32,
    P4_GAME_PACKAGE_VERSION_BYTES = 16,
    P4_GAME_PACKAGE_LICENSE_BYTES = 16,
    P4_GAME_PACKAGE_MAX_BYTES = 512 * 1024,
};

#define P4_GAME_PACKAGE_MAGIC "P4GAME1\0"
#define P4_GAME_PACKAGE_FORMAT_VERSION UINT32_C(1)
#define P4_GAME_PACKAGE_API_VERSION UINT32_C(1)
#define P4_GAME_PACKAGE_FLAG_DEVELOPMENT UINT16_C(0x0001)
#define P4_GAME_PACKAGE_FLAG_MULTIPLAYER_PROFILE UINT16_C(0x0002)

typedef struct {
    uint32_t package_bytes;
    uint32_t payload_offset;
    uint32_t payload_bytes;
    uint32_t launcher_id;
    uint32_t required_capabilities;
    uint32_t optional_capabilities;
    uint16_t accent_rgb565;
    uint16_t flags;
    bool multiplayer_profile_declared;
    p4_game_multiplayer_profile_t multiplayer_profile;
    uint8_t payload_sha256[P4_GAME_PACKAGE_SHA256_BYTES];
    char id[P4_GAME_PACKAGE_ID_BYTES];
    char title[P4_GAME_PACKAGE_TITLE_BYTES];
    char subtitle[P4_GAME_PACKAGE_SUBTITLE_BYTES];
    char folder[P4_GAME_PACKAGE_FOLDER_BYTES];
    char version[P4_GAME_PACKAGE_VERSION_BYTES];
    char license[P4_GAME_PACKAGE_LICENSE_BYTES];
} p4_game_package_info_t;

enum {
    P4_GAME_ICON_WIDTH = 128,
    P4_GAME_ICON_HEIGHT = 72,
    P4_GAME_ICON_PIXELS = P4_GAME_ICON_WIDTH * P4_GAME_ICON_HEIGHT,
    P4_GAME_ICON_BYTES = 16 + 512 + P4_GAME_ICON_PIXELS,
};
typedef struct {
    uint16_t palette[256];
    uint8_t pixels[P4_GAME_ICON_PIXELS];
} p4_game_icon_t;
/** Read an optional .p4icon ELF section after the package digest is verified.
 * Fixed geometry and palette; malformed or absent artwork returns false. */
bool p4_game_package_read_icon(const uint8_t *elf, size_t bytes, p4_game_icon_t *out);

typedef enum {
    P4_GAME_PACKAGE_VALID = 0,
    P4_GAME_PACKAGE_BAD_ARGUMENT,
    P4_GAME_PACKAGE_BAD_SIZE,
    P4_GAME_PACKAGE_BAD_MAGIC,
    P4_GAME_PACKAGE_BAD_VERSION,
    P4_GAME_PACKAGE_BAD_LAYOUT,
    P4_GAME_PACKAGE_BAD_METADATA,
    P4_GAME_PACKAGE_BAD_ELF,
    P4_GAME_PACKAGE_BAD_DIGEST,
} p4_game_package_result_t;

p4_game_package_result_t p4_game_package_parse(
    const uint8_t *data, size_t size_bytes, p4_game_package_info_t *out_info);

p4_game_package_result_t p4_game_package_validate_elf(
    const uint8_t *elf, size_t size_bytes);

const char *p4_game_package_result_name(p4_game_package_result_t result);

#ifdef __cplusplus
}
#endif

#endif
