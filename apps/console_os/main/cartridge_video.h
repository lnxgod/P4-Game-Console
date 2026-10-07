// SPDX-License-Identifier: MIT
#ifndef CONSOLE_CARTRIDGE_VIDEO_H
#define CONSOLE_CARTRIDGE_VIDEO_H

#include "p4/game.h"

typedef enum {
    CARTRIDGE_VIDEO_TAB5,
    CARTRIDGE_VIDEO_LEGACY_HIGH_RES,
    CARTRIDGE_VIDEO_LEGACY,
} cartridge_video_policy_t;

/* Negotiate before entering a cartridge. Input coordinates remain 320x200. */
static inline bool cartridge_video_select(
    cartridge_video_policy_t policy, uint32_t required, uint32_t optional,
    uint16_t *width, uint16_t *height)
{
    if (width == NULL || height == NULL) return false;
    *width = 0U;
    *height = 0U;
    const bool native_required = (required & P4_GAME_CAP_VIDEO_HIGH_RES) != 0U;
    const bool native_supported =
        ((required | optional) & P4_GAME_CAP_VIDEO_HIGH_RES) != 0U;
    bool native = false;
    switch (policy) {
    case CARTRIDGE_VIDEO_TAB5:
        if (!native_required) return false;
        native = true;
        break;
    case CARTRIDGE_VIDEO_LEGACY_HIGH_RES:
        native = native_supported;
        break;
    case CARTRIDGE_VIDEO_LEGACY:
        if (native_required) return false;
        break;
    default:
        return false;
    }
    *width = native ? P4_GAME_SURFACE_HIGH_RES_WIDTH : P4_GAME_SURFACE_WIDTH;
    *height = native ? P4_GAME_SURFACE_HIGH_RES_HEIGHT : P4_GAME_SURFACE_HEIGHT;
    return true;
}

#endif
