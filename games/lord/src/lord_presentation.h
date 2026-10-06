// SPDX-License-Identifier: LicenseRef-LORD-Permission
/* Red Dragon's canonical 320x200 layout is also its touch contract. Paint
 * semantic primitives straight into the negotiated surface, never a scaled
 * intermediate frame. No presentation data enters the save or realm state. */
#include "p4/presentation.h"
#include "p4/cp437.h"

static bool lord_native(const p4_game_surface_t *s)
{
    return s->width == P4_GAME_SURFACE_HIGH_RES_WIDTH;
}

/* Two offline-sized indexed images avoid runtime scaling and framebuffer
 * allocation. The inner loop is a byte lookup, palette lookup and store. */
static void lord_title_illustration(p4_game_surface_t *s)
{
    const bool native = lord_native(s);
    const unsigned x = native ? 16U : 8U, y = native ? 64U : 28U;
    const unsigned w = native ? LORD_TITLE_NATIVE_W : LORD_TITLE_FALLBACK_W;
    const unsigned h = native ? LORD_TITLE_NATIVE_H : LORD_TITLE_FALLBACK_H;
    const uint8_t *src = native ? s_lord_title_native_pixels : s_lord_title_fallback_pixels;
    const uint16_t *palette = native ? s_lord_title_native_palette : s_lord_title_fallback_palette;
    if (s->width < x + w || s->height < y + h || s->stride_pixels < s->width) return;
    uint16_t *row = s->pixels + (size_t)y * s->stride_pixels + x;
    for (unsigned py = 0; py < h; ++py) {
        for (unsigned px = 0; px < w; ++px) row[px] = palette[*src++];
        row += s->stride_pixels;
    }
}

static uint16_t lord_ink(const p4_game_surface_t *s, uint16_t color)
{
    if (!lord_native(s)) return color;
    /* Midnight ink, ivory type, garnet selections and warm brass. Preserve
     * semantic ANSI color families, including literal monster-name colors. */
    switch (color) {
    case ANSI_BLACK: return 0x0842;
    case ANSI_PANEL: return 0x10a4;
    case ANSI_PANEL_ALT: return 0x18e6;
    case ANSI_BLUE: return 0x2a70;
    case ANSI_BRIGHT_BLUE: return 0x6c15;
    case ANSI_RED: return 0x6907;
    case ANSI_BRIGHT_RED: return 0xfb0d;
    case ANSI_YELLOW: return 0xeead;
    case ANSI_BROWN: return 0x6b0a;
    case ANSI_LIGHT_GRAY: return 0xc658;
    case ANSI_DARK_GRAY: return 0x8492;
    case ANSI_WHITE: return 0xf7ba;
    case ANSI_CYAN: return 0x3c74;
    case ANSI_BRIGHT_CYAN: return 0x7e39;
    case ANSI_GREEN: return 0x3c0e;
    case ANSI_BRIGHT_GREEN: return 0x96b2;
    case ANSI_MAGENTA: return 0x91b3;
    case ANSI_BRIGHT_MAGENTA: return 0xdcd8;
    default: return color;
    }
}

static void lord_fill(p4_game_surface_t *s, int x, int y, int w, int h,
                      uint16_t color)
{
    const int left = p4_ui_x(s, x), top = p4_ui_y(s, y);
    p4_draw_fill_rect(s, left, top, p4_ui_x(s, x + w) - left,
                     p4_ui_y(s, y + h) - top, lord_ink(s, color));
}

static void lord_rect(p4_game_surface_t *s, int x, int y, int w, int h,
                      uint16_t color)
{
    const int left = p4_ui_x(s, x), top = p4_ui_y(s, y);
    p4_draw_rect(s, left, top, p4_ui_x(s, x + w) - left,
                 p4_ui_y(s, y + h) - top, lord_ink(s, color));
}

/* Full 16-row CP437 contours survive at native size even in a short scene
 * cell. Resolve the tiny sampling map once, then write bounded row spans.
 * This uses the same pinned font already linked by p4/draw.h. */
static void lord_glyph(p4_game_surface_t *s, int x, int y, uint8_t ch,
                       uint16_t front, uint16_t back, unsigned cell_height)
{
    if (!lord_native(s)) {
        p4_draw_cp437_glyph(s, x, y, ch, front, back, cell_height);
        return;
    }
    if ((cell_height != 8U && cell_height != 16U) ||
        x < -320 || x > 320 || y < -200 || y > 200) return;
    const int left = p4_ui_x(s, x), top = p4_ui_y(s, y);
    const int w = p4_ui_x(s, x + 8) - left;
    const int h = p4_ui_y(s, y + (int)cell_height) - top;
    const int x0 = left < 0 ? -left : 0;
    const int y0 = top < 0 ? -top : 0;
    const int x1 = left + w > s->width ? (int)s->width - left : w;
    const int y1 = top + h > s->height ? (int)s->height - top : h;
    if (x0 >= x1 || y0 >= y1) return;
    front = lord_ink(s, front);
    back = lord_ink(s, back);
    uint8_t masks[20];
    for (int px = x0; px < x1; ++px)
        masks[px] = (uint8_t)(0x80U >> ((unsigned)px * 8U / (unsigned)w));
    const uint8_t *glyph = p4_cp437_font_8x16 + (unsigned)ch * 16U;
    for (int py = y0; py < y1; ++py) {
        const uint8_t bits = glyph[(unsigned)py * 16U / (unsigned)h];
        uint16_t *dst = s->pixels + (size_t)(top + py) * s->stride_pixels +
                        (size_t)(left + x0);
        for (int px = x0; px < x1; ++px)
            *dst++ = (bits & masks[px]) != 0U ? front : back;
    }
}

static unsigned lord_text_height(const char *text, int available)
{
    unsigned height = 22U;
    while (height > 16U && p4_ui_text_width(text, height, 50U) > available)
        --height;
    return height;
}

static void lord_native_text(p4_game_surface_t *s, int x, int y,
                             const char *text, uint16_t color, int right)
{
    const int left = p4_ui_x(s, x);
    const unsigned height = lord_text_height(text, p4_ui_x(s, right) - left);
    p4_ui_text(s, left, p4_ui_y(s, y) - 3, text,
               lord_ink(s, color), height, 50U);
}
