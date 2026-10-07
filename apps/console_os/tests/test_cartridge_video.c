// SPDX-License-Identifier: MIT
#include "cartridge_video.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    const uint32_t base = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS;
    const uint32_t high = P4_GAME_CAP_VIDEO_HIGH_RES;
    const uint32_t audio = P4_GAME_CAP_AUDIO_TONE;
    uint16_t width = 123U, height = 456U;
    /* A maintained launch always yields the actual native surface. */
    assert(cartridge_video_select(CARTRIDGE_VIDEO_TAB5, base | high, audio,
                                  &width, &height));
    assert(width == 768U && height == 480U);
    /* Old optional-only or unsupported cartridges fail before entry. */
    assert(!cartridge_video_select(CARTRIDGE_VIDEO_TAB5, base, high | audio,
                                   &width, &height));
    assert(width == 0U && height == 0U);
    assert(!cartridge_video_select(CARTRIDGE_VIDEO_TAB5, base, audio,
                                   &width, &height));
    assert(width == 0U && height == 0U);
    /* Preserve negotiated legacy board and ABI behavior. */
    assert(cartridge_video_select(CARTRIDGE_VIDEO_LEGACY_HIGH_RES, base, high,
                                  &width, &height));
    assert(width == 768U && height == 480U);
    assert(cartridge_video_select(CARTRIDGE_VIDEO_LEGACY_HIGH_RES, base, audio,
                                  &width, &height));
    assert(width == 320U && height == 200U);
    assert(cartridge_video_select(CARTRIDGE_VIDEO_LEGACY, base, high,
                                  &width, &height));
    assert(width == 320U && height == 200U);
    assert(!cartridge_video_select(CARTRIDGE_VIDEO_LEGACY, base | high, audio,
                                   &width, &height));
    assert(width == 0U && height == 0U);
    assert(!cartridge_video_select((cartridge_video_policy_t)99, base | high, 0U,
                                   &width, &height));
    assert(!cartridge_video_select(CARTRIDGE_VIDEO_TAB5, base | high, 0U,
                                   NULL, &height));
    assert(!cartridge_video_select(CARTRIDGE_VIDEO_TAB5, base | high, 0U,
                                   &width, NULL));
    puts("PASS native cartridge selection and legacy capability negotiation");
    return 0;
}
