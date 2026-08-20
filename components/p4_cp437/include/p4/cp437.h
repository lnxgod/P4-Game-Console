// SPDX-License-Identifier: MIT

#ifndef P4_CP437_H
#define P4_CP437_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_CP437_GLYPH_COUNT = 256,
    P4_CP437_GLYPH_WIDTH = 8,
    P4_CP437_GLYPH_HEIGHT = 16,
    P4_CP437_FONT_BYTES =
        P4_CP437_GLYPH_COUNT * P4_CP437_GLYPH_HEIGHT,
};

/** Pinned IBM PC Code Page 437 glyph rows, MSB-first, 16 rows per glyph. */
extern const uint8_t p4_cp437_font_8x16[P4_CP437_FONT_BYTES];

#ifdef __cplusplus
}
#endif

#endif
