// SPDX-License-Identifier: MIT
/* Test-only original raster formulas: independent pixel oracle for RV32
 * optimizations. Keep these straightforward, rather than sharing optimized
 * clipping, quotient stepping or span implementations with production. */
static uint16_t reference_p4_ui_blend(uint16_t back, uint16_t front, unsigned a)
{
    if (a >= 15U) return front;
    const unsigned inv = 15U - a;
    const unsigned r = (((unsigned)(back >> 11U) * inv) +
        ((unsigned)(front >> 11U) * a) + 7U) / 15U;
    const unsigned g = ((((unsigned)back >> 5U & 63U) * inv) +
        (((unsigned)front >> 5U & 63U) * a) + 7U) / 15U;
    const unsigned b = ((((unsigned)back & 31U) * inv) +
        (((unsigned)front & 31U) * a) + 7U) / 15U;
    return (uint16_t)((r << 11U) | (g << 5U) | b);
}

static void reference_p4_ui_text(p4_game_surface_t *s, int x, int y,
                               const char *text, uint16_t color,
                               unsigned height, size_t max_chars)
{
    if (!p4_surface_valid(s) || text == NULL ||
        x < -32768 || x > 32767 || y < -32768 || y > 32767) return;
    height = p4_ui_font_height(height);
    const unsigned width = (24U * height + 14U) / 28U;
    if (max_chars > 256U) max_chars = 256U;
    for (size_t i = 0; i < max_chars && text[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (ch < 32U || ch > 126U) ch = 63U;
        for (unsigned py = 0; py < height; ++py) {
            const int dy = y + (int)py;
            if (dy < 0 || dy >= (int)s->height) continue;
            for (unsigned px = 0; px < width; ++px) {
                const int dx = x + (int)px;
                if (dx < 0 || dx >= (int)s->width) continue;
                const unsigned a = p4_ui_glyph_alpha(ch, px * 24U / width,
                                                     py * 28U / height);
                if (a != 0U) {
                    uint16_t *pixel = &s->pixels[(size_t)dy * s->stride_pixels + (size_t)dx];
                    *pixel = reference_p4_ui_blend(*pixel, color, a);
                }
            }
        }
        x += (int)(((unsigned)p4_ui_font_advance[ch - 32U] * height + 14U) / 28U);
        if (x >= (int)s->width) break;
    }
}

static void reference_p4_ui_round_rect(p4_game_surface_t *s, int x, int y,
                                    int width, int height, int radius,
                                    uint16_t color)
{
    if (!p4_surface_valid(s) || width <= 0 || height <= 0 ||
        width > 4096 || height > 4096 || x < -8192 || x > 8192 ||
        y < -8192 || y > 8192) return;
    if (radius < 0) radius = 0;
    if (radius > width / 2) radius = width / 2;
    if (radius > height / 2) radius = height / 2;
    for (int row = 0; row < height; ++row) {
        if (y + row < 0 || y + row >= (int)s->height) continue;
        int inset = 0;
        const int edge = row < height / 2 ? row : height - 1 - row;
        if (edge < radius) {
            const int dy = radius - 1 - edge;
            while (inset < radius &&
                   (radius - inset) * (radius - inset) + dy * dy > radius * radius) ++inset;
        }
        p4_draw_fill_rect(s, x + inset, y + row, width - inset * 2, 1, color);
    }
}

static void reference_p4_ui_sprite(p4_game_surface_t *s, int x, int y,
                                int width, int height, const uint16_t *pixels,
                                int source_width, int source_height,
                                bool key_enabled, uint16_t key)
{
    if (!p4_surface_valid(s) || pixels == NULL || width <= 0 || height <= 0 ||
        source_width <= 0 || source_height <= 0 || width > 4096 || height > 4096 ||
        source_width > 4096 || source_height > 4096 ||
        x < -8192 || x > 8192 || y < -8192 || y > 8192) return;
    const int start_x = x < 0 ? -x : 0;
    const int start_y = y < 0 ? -y : 0;
    const int end_x = x + width > (int)s->width ? (int)s->width - x : width;
    const int end_y = y + height > (int)s->height ? (int)s->height - y : height;
    for (int row = start_y; row < end_y; ++row) {
        const size_t sy = (size_t)(row * source_height / height);
        uint16_t *dst = &s->pixels[(size_t)(y + row) * s->stride_pixels];
        for (int col = start_x; col < end_x; ++col) {
            const size_t sx = (size_t)(col * source_width / width);
            const uint16_t value = pixels[sy * (size_t)source_width + sx];
            if (!key_enabled || value != key) dst[x + col] = value;
        }
    }
}

static void reference_p4_card_suit(p4_game_surface_t *s,int x,int y,int size,unsigned suit,uint16_t color)
{
    if(!p4_surface_valid(s)||size<3||size>512||x < -8192||x > 8192||y < -8192||y > 8192)return;
    for(int py=0;py<size;++py) for(int px=0;px<size;++px){
        const int u=px*100/size-50, v=py*100/size;
        bool inside=false;
        if(suit==1U) inside=(u<0?-u:u)*2+(v<50?50-v:v-50)<83;
        else if(suit==2U){
            const int a=u+22,b=u-22,d=v-28;
            inside=(a*a+d*d<27*27)||(b*b+d*d<27*27)||(v>=28 && v<91 && (u<0?-u:u)<(91-v)*49/63);
        }else if(suit==0U){
            const int d=v-27,e=v-54,a=u+24,b=u-24;
            inside=u*u+d*d<23*23 || a*a+e*e<24*24 || b*b+e*e<24*24 || (v>46 && v<87 && (u<0?-u:u)<8) || (v>=82 && v<91 && (u<0?-u:u)<20);
        }else{
            const int a=u+21,b=u-21,d=v-53;
            inside=(v<53 && v>5 && (u<0?-u:u)<(v-5)*45/48) || a*a+d*d<24*24 || b*b+d*d<24*24 || (v>51 && v<88 && (u<0?-u:u)<8) || (v>=83 && v<92 && (u<0?-u:u)<20);
        }
        if(inside)p4_draw_pixel(s,x+px,y+py,color);
    }
}

static void reference_p4_draw_fill_circle(p4_game_surface_t *surface,
                         int center_x, int center_y, int radius,
                         uint16_t color)
{
    if (!p4_surface_valid(surface) || radius < 0 || radius > 1024) {
        return;
    }
    const int64_t radius_squared = (int64_t)radius * radius;
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if ((int64_t)x * x + (int64_t)y * y <= radius_squared) {
                p4_draw_pixel(surface, center_x + x, center_y + y, color);
            }
        }
    }
}

static void reference_p4_draw_sprite_rgb565(p4_game_surface_t *surface,
                           int x, int y,
                           const uint16_t *pixels,
                           size_t width, size_t height,
                           size_t stride_pixels,
                           bool use_transparency,
                           uint16_t transparent_color)
{
    if (!p4_surface_valid(surface) || pixels == NULL || width == 0U ||
        height == 0U || width > 4096U || height > 4096U ||
        stride_pixels < width) {
        return;
    }
    for (size_t row = 0U; row < height; ++row) {
        for (size_t column = 0U; column < width; ++column) {
            const uint16_t color = pixels[row * stride_pixels + column];
            if (!use_transparency || color != transparent_color) {
                p4_draw_pixel(surface,
                              x + (int)column,
                              y + (int)row,
                              color);
            }
        }
    }
}

static void reference_p4_draw_sprite(p4_game_surface_t *surface, int x, int y,
                    const p4_sprite_t *sprite)
{
    if (!p4_surface_valid(surface) || !p4_sprite_valid(sprite)) {
        return;
    }
    for (size_t row = 0U; row < sprite->height; ++row) {
        const size_t source_row =
            (sprite->flip & P4_SPRITE_FLIP_Y) != 0U
            ? sprite->height - row - 1U : row;
        for (size_t column = 0U; column < sprite->width; ++column) {
            const size_t source_column =
                (sprite->flip & P4_SPRITE_FLIP_X) != 0U
                ? sprite->width - column - 1U : column;
            const uint16_t color = sprite->pixels[
                (sprite->source_y + source_row) * sprite->stride_pixels +
                sprite->source_x + source_column];
            if (sprite->use_transparency &&
                color == sprite->transparent_color) {
                continue;
            }
            const int64_t left = (int64_t)x +
                (int64_t)column * sprite->scale;
            const int64_t top = (int64_t)y +
                (int64_t)row * sprite->scale;
            for (uint8_t repeat_y = 0U;
                 repeat_y < sprite->scale; ++repeat_y) {
                const int64_t destination_y = top + repeat_y;
                if (destination_y < 0 ||
                    destination_y >= surface->height) {
                    continue;
                }
                uint16_t *const destination = surface->pixels +
                    (size_t)destination_y * surface->stride_pixels;
                for (uint8_t repeat_x = 0U;
                     repeat_x < sprite->scale; ++repeat_x) {
                    const int64_t destination_x = left + repeat_x;
                    if (destination_x >= 0 &&
                        destination_x < surface->width) {
                        destination[(size_t)destination_x] = color;
                    }
                }
            }
        }
    }
}
