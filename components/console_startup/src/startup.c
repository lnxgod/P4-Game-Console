// SPDX-License-Identifier: MIT
#include "console/startup.h"
#include "console/ui_font.h"
#include <string.h>

static uint16_t rgb(unsigned r, unsigned g, unsigned b)
{ return (uint16_t)(((r >> 3U) << 11U) | ((g >> 2U) << 5U) | (b >> 3U)); }

/* Coordinates are authored at 768x480, then rasterized directly at the output
 * resolution. The panel never enlarges this UI framebuffer on Tab5. */
static void rect(uint16_t *p, size_t stride, size_t w, size_t h,
    unsigned x, unsigned y, unsigned rw, unsigned rh, uint16_t c)
{
    const size_t x0 = x * w / 768U, y0 = y * h / 480U;
    size_t x1 = (x + rw) * w / 768U, y1 = (y + rh) * h / 480U;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    for (size_t py = y0; py < y1; ++py)
        for (size_t px = x0; px < x1; ++px) p[py * stride + px] = c;
}
static void text(uint16_t *p, size_t stride, size_t w, size_t h,
    unsigned x, unsigned y, const char *s, unsigned scale, uint16_t color,
    size_t max_chars)
{
    size_t left = x * w / 768U;
    const size_t top = y * h / 480U;
    const size_t gh = 16U * scale * h / 480U;
    const size_t gw = gh * 24U / 28U;
    for (size_t i = 0U; i < max_chars && s[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32U || c > 126U) c = '?';
        for (size_t py = 0U; py < gh && top + py < h; ++py) {
            for (size_t px = 0U; px < gw && left + px < w; ++px) {
                const unsigned alpha = console_ui_glyph_alpha(c,
                    (unsigned)(px * 24U / gw), (unsigned)(py * 28U / gh));
                if (alpha == 0U) continue;
                uint16_t *dst = &p[(top + py) * stride + left + px];
                const unsigned inv = 15U - alpha;
                const unsigned r = (((unsigned)color >> 11U) * alpha + ((unsigned)*dst >> 11U) * inv + 7U) / 15U;
                const unsigned g = ((((unsigned)color >> 5U) & 63U) * alpha + (((unsigned)*dst >> 5U) & 63U) * inv + 7U) / 15U;
                const unsigned b = (((unsigned)color & 31U) * alpha + ((unsigned)*dst & 31U) * inv + 7U) / 15U;
                *dst = (uint16_t)((r << 11U) | (g << 5U) | b);
            }
        }
        left += console_ui_font_advance[c - 32U] * gh / 28U;
    }
}

bool console_startup_render(uint16_t *p, size_t stride, size_t w,
    size_t h, const uint8_t *logo, size_t logo_bytes,
    unsigned animation, const char *status, bool repaint)
{
    if (!p || !logo || logo_bytes != CONSOLE_STARTUP_LOGO_BYTES ||
        !((w == 768U && h == 480U) || (w == 1152U && h == 720U) ||
          (w == 1280U && h == 720U)) ||
        stride < w || stride > SIZE_MAX / h / sizeof(*p)) return false;
    const uint16_t cyan = rgb(85,230,236), white = rgb(244,250,252);
    const uint16_t muted = rgb(180,203,213), navy = rgb(7,18,28);
    if (repaint) {
        for (size_t y = 0; y < h; ++y) {
            const unsigned t = (unsigned)(y * 10U / h);
            for (size_t x = 0; x < w; ++x)
                p[y * stride + x] = rgb(7U,18U+t/2U,28U+t);
        }
        /* A restrained perspective grid keeps the mark and status readable. */
        for (unsigned y = 294; y < 408; y += (y-260U)/4U)
            rect(p,stride,w,h,0,y,768,1,rgb(15,40,50));
        for (unsigned y = 290; y < 408; ++y) {
            for (int line = -8; line <= 8; ++line) {
                const int x = 384 + line * (int)(y-255U) / 2;
                if (x >= 0 && x < 768)
                    rect(p,stride,w,h,(unsigned)x,y,1,1,rgb(15,40,50));
            }
        }
        /* Reviewed GameChangersAI joystick mark, independent of the UI text. */
        const size_t side = 192U * h / 480U;
        const size_t left = (w-side)/2U, top = 34U * h / 480U;
        for (size_t y = 0; y < side; ++y) {
            for (size_t x = 0; x < side; ++x) {
                const size_t i = (y * 384U / side * 384U + x * 384U / side) * 3U;
                const unsigned alpha = logo[i+2U], inv = 255U-alpha;
                if (alpha == 0U) continue;
                const unsigned fg = (unsigned)logo[i] | ((unsigned)logo[i+1U] << 8U);
                uint16_t *dst = &p[(top+y)*stride+left+x];
                const unsigned r = ((fg>>11U)*alpha+((unsigned)*dst>>11U)*inv+127U)/255U;
                const unsigned g = (((fg>>5U)&63U)*alpha+(((unsigned)*dst>>5U)&63U)*inv+127U)/255U;
                const unsigned b = ((fg&31U)*alpha+((unsigned)*dst&31U)*inv+127U)/255U;
                *dst=(uint16_t)((r<<11U)|(g<<5U)|b);
            }
        }
        const char *brand="GameChangersAI";
        size_t advance=0;
        const size_t gh=48U*h/480U;
        for(size_t i=0;brand[i];++i) advance+=console_ui_font_advance[(unsigned char)brand[i]-32U]*gh/28U;
        const unsigned title_x=(unsigned)((w-advance)/2U*768U/w);
        text(p,stride,w,h,title_x,245,brand,3,white,14);
        text(p,stride,w,h,334,305,"OS " CONSOLE_PRODUCT_VERSION,1,cyan,20);
        rect(p,stride,w,h,344,356,80,2,cyan);
    }
    rect(p,stride,w,h,0,408,768,72,navy);
    text(p,stride,w,h,28,423,status ? status : "Starting...",1,muted,70);
    /* This animates while services start; it adds no boot delay. */
    rect(p,stride,w,h,28,461,712,2,rgb(25,52,63));
    if(animation==CONSOLE_STARTUP_COMPLETE){
        rect(p,stride,w,h,28,461,712,2,cyan);
    }else{
        /* Reach both ends, then reverse without jumping back across the track. */
        unsigned phase=animation%62U;if(phase>31U)phase=62U-phase;
        const unsigned position=phase*(712U-72U)/31U;
        rect(p,stride,w,h,28+position,461,72,2,cyan);
    }
    return true;
}
