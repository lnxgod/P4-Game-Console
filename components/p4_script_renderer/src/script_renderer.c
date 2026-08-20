// SPDX-License-Identifier: MIT

#include "p4/script_renderer.h"

#include <limits.h>
#include <string.h>

#include "p4/cp437.h"

enum {
    P4_SCRIPT_RENDER_PIXEL_BUDGET =
        P4_SCRIPT_SCREEN_WIDTH * P4_SCRIPT_SCREEN_HEIGHT * 2U,
    P4_SCRIPT_MISSING_SPRITE_SIZE = 16,
};

typedef struct {
    p4_script_surface_t *surface;
    p4_script_render_stats_t stats;
    uint32_t pixels_remaining;
} renderer_t;

static bool surface_valid(const p4_script_surface_t *surface)
{
    return surface != NULL && surface->pixels != NULL &&
        surface->width == P4_SCRIPT_SCREEN_WIDTH &&
        surface->height == P4_SCRIPT_SCREEN_HEIGHT &&
        surface->stride_pixels >= P4_SCRIPT_SCREEN_WIDTH;
}

static bool spend_pixels(renderer_t *renderer, uint32_t count)
{
    if (count > renderer->pixels_remaining) {
        renderer->stats.pixel_budget_exhausted = true;
        return false;
    }
    renderer->pixels_remaining -= count;
    renderer->stats.pixel_budget_used += count;
    return true;
}

static void pixel(renderer_t *renderer, int32_t x, int32_t y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= (int32_t)renderer->surface->width ||
        y >= (int32_t)renderer->surface->height ||
        !spend_pixels(renderer, 1U)) {
        return;
    }
    renderer->surface->pixels[
        (size_t)y * renderer->surface->stride_pixels + (size_t)x] = color;
}

static bool clipped_rect(
    const p4_script_surface_t *surface,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    int32_t *left_out,
    int32_t *top_out,
    int32_t *right_out,
    int32_t *bottom_out)
{
    if (width <= 0 || height <= 0) {
        return false;
    }
    int64_t left = x;
    int64_t top = y;
    int64_t right = left + (int64_t)width;
    int64_t bottom = top + (int64_t)height;
    if (right <= 0 || bottom <= 0 || left >= surface->width ||
        top >= surface->height) {
        return false;
    }
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > surface->width) right = surface->width;
    if (bottom > surface->height) bottom = surface->height;
    *left_out = (int32_t)left;
    *top_out = (int32_t)top;
    *right_out = (int32_t)right;
    *bottom_out = (int32_t)bottom;
    return left < right && top < bottom;
}

static bool fill_rect(
    renderer_t *renderer,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint16_t color)
{
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    if (!clipped_rect(
            renderer->surface, x, y, width, height,
            &left, &top, &right, &bottom)) {
        return true;
    }
    const uint32_t clipped_width = (uint32_t)(right - left);
    const uint32_t clipped_height = (uint32_t)(bottom - top);
    const uint32_t pixel_count = clipped_width * clipped_height;
    if (!spend_pixels(renderer, pixel_count)) {
        return false;
    }
    for (int32_t row = top; row < bottom; ++row) {
        uint16_t *destination = &renderer->surface->pixels[
            (size_t)row * renderer->surface->stride_pixels + (size_t)left];
        for (uint32_t column = 0U; column < clipped_width; ++column) {
            destination[column] = color;
        }
    }
    return true;
}

static bool outline_rect(
    renderer_t *renderer,
    const p4_script_render_command_t *command)
{
    const int64_t bottom64 =
        (int64_t)command->y + command->height - 1;
    const int64_t right64 =
        (int64_t)command->x + command->width - 1;
    const int32_t bottom = bottom64 > INT32_MAX ? INT32_MAX
        : bottom64 < INT32_MIN ? INT32_MIN : (int32_t)bottom64;
    const int32_t right = right64 > INT32_MAX ? INT32_MAX
        : right64 < INT32_MIN ? INT32_MIN : (int32_t)right64;
    if (!fill_rect(renderer, command->x, command->y,
                   command->width, 1, command->color_rgb565) ||
        !fill_rect(renderer, command->x, bottom,
                   command->width, 1, command->color_rgb565) ||
        !fill_rect(renderer, command->x, command->y,
                   1, command->height, command->color_rgb565) ||
        !fill_rect(renderer, right, command->y,
                   1, command->height,
                   command->color_rgb565)) {
        return false;
    }
    return true;
}

enum {
    CLIP_LEFT = 1,
    CLIP_RIGHT = 2,
    CLIP_TOP = 4,
    CLIP_BOTTOM = 8,
};

static uint8_t line_clip_code(int64_t x, int64_t y)
{
    uint8_t code = 0U;
    if (x < 0) code |= CLIP_LEFT;
    else if (x >= P4_SCRIPT_SCREEN_WIDTH) code |= CLIP_RIGHT;
    if (y < 0) code |= CLIP_TOP;
    else if (y >= P4_SCRIPT_SCREEN_HEIGHT) code |= CLIP_BOTTOM;
    return code;
}

static int64_t line_intersection(
    int64_t origin, int64_t delta, int64_t numerator, int64_t denominator)
{
    if (denominator == 0) {
        return origin;
    }
    const long double value = (long double)origin +
        ((long double)delta * (long double)numerator) /
            (long double)denominator;
    if (value > (long double)INT64_MAX) return INT64_MAX;
    if (value < (long double)INT64_MIN) return INT64_MIN;
    return (int64_t)value;
}

static bool clip_line(int32_t *x0_out, int32_t *y0_out,
                      int32_t *x1_out, int32_t *y1_out)
{
    int64_t x0 = *x0_out;
    int64_t y0 = *y0_out;
    int64_t x1 = *x1_out;
    int64_t y1 = *y1_out;
    for (unsigned attempt = 0U; attempt < 8U; ++attempt) {
        const uint8_t code0 = line_clip_code(x0, y0);
        const uint8_t code1 = line_clip_code(x1, y1);
        if ((code0 | code1) == 0U) {
            *x0_out = (int32_t)x0;
            *y0_out = (int32_t)y0;
            *x1_out = (int32_t)x1;
            *y1_out = (int32_t)y1;
            return true;
        }
        if ((code0 & code1) != 0U) {
            return false;
        }
        const uint8_t code = code0 != 0U ? code0 : code1;
        int64_t x = 0;
        int64_t y = 0;
        const int64_t dx = x1 - x0;
        const int64_t dy = y1 - y0;
        if ((code & CLIP_TOP) != 0U) {
            if (dy == 0) return false;
            y = 0;
            x = line_intersection(x0, dx, y - y0, dy);
        } else if ((code & CLIP_BOTTOM) != 0U) {
            if (dy == 0) return false;
            y = P4_SCRIPT_SCREEN_HEIGHT - 1;
            x = line_intersection(x0, dx, y - y0, dy);
        } else if ((code & CLIP_RIGHT) != 0U) {
            if (dx == 0) return false;
            x = P4_SCRIPT_SCREEN_WIDTH - 1;
            y = line_intersection(y0, dy, x - x0, dx);
        } else {
            if (dx == 0) return false;
            x = 0;
            y = line_intersection(y0, dy, x - x0, dx);
        }
        if (code == code0) {
            x0 = x;
            y0 = y;
        } else {
            x1 = x;
            y1 = y;
        }
    }
    return false;
}

static bool line(renderer_t *renderer, const p4_script_render_command_t *command)
{
    int32_t x0 = command->x;
    int32_t y0 = command->y;
    int32_t x1 = command->width;
    int32_t y1 = command->height;
    if (!clip_line(&x0, &y0, &x1, &y1)) {
        return true;
    }
    const int32_t dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    const int32_t sx = x0 < x1 ? 1 : -1;
    const int32_t dy_abs = y1 >= y0 ? y1 - y0 : y0 - y1;
    const int32_t dy = -dy_abs;
    const int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        pixel(renderer, x0, y0, command->color_rgb565);
        if (renderer->stats.pixel_budget_exhausted) return false;
        if (x0 == x1 && y0 == y1) break;
        const int64_t twice_error = (int64_t)error * 2;
        if (twice_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
    return true;
}

static bool circle(renderer_t *renderer,
                   const p4_script_render_command_t *command)
{
    const int64_t radius = command->width;
    const int64_t left64 = (int64_t)command->x - radius;
    const int64_t right64 = (int64_t)command->x + radius;
    const int64_t top64 = (int64_t)command->y - radius;
    const int64_t bottom64 = (int64_t)command->y + radius;
    if (radius <= 0 || right64 < 0 || bottom64 < 0 ||
        left64 >= (int64_t)P4_SCRIPT_SCREEN_WIDTH ||
        top64 >= (int64_t)P4_SCRIPT_SCREEN_HEIGHT) {
        return true;
    }
    const int32_t left = left64 < 0 ? 0 : (int32_t)left64;
    const int32_t right = right64 >= (int64_t)P4_SCRIPT_SCREEN_WIDTH
        ? (int32_t)(P4_SCRIPT_SCREEN_WIDTH - 1U) : (int32_t)right64;
    const int32_t top = top64 < 0 ? 0 : (int32_t)top64;
    const int32_t bottom = bottom64 >= (int64_t)P4_SCRIPT_SCREEN_HEIGHT
        ? (int32_t)(P4_SCRIPT_SCREEN_HEIGHT - 1U) : (int32_t)bottom64;
    const int64_t radius_squared = radius * radius;
    const int64_t inner_radius = radius - 1;
    const int64_t inner_squared = inner_radius * inner_radius;
    for (int32_t y = top; y <= bottom; ++y) {
        for (int32_t x = left; x <= right; ++x) {
            const int64_t dx = (int64_t)x - command->x;
            const int64_t dy = (int64_t)y - command->y;
            const int64_t distance = dx * dx + dy * dy;
            const bool inside = distance <= radius_squared;
            const bool draw = (command->flags & P4_SCRIPT_RENDER_FLAG_FILLED)
                != 0U ? inside : inside && distance >= inner_squared;
            if (draw) {
                pixel(renderer, x, y, command->color_rgb565);
                if (renderer->stats.pixel_budget_exhausted) return false;
            }
        }
    }
    return true;
}

static uint8_t next_glyph(const uint8_t *text, uint32_t bytes,
                          uint32_t *offset)
{
    const uint8_t first = text[(*offset)++];
    if (first < 0x80U) {
        return first;
    }
    while (*offset < bytes && (text[*offset] & 0xc0U) == 0x80U) {
        ++*offset;
    }
    return (uint8_t)'?';
}

static bool text(renderer_t *renderer,
                 const p4_script_render_packet_t *packet,
                 const p4_script_render_command_t *command)
{
    const uint8_t *bytes = &packet->text[command->asset_id];
    uint32_t offset = 0U;
    int64_t pen_x = command->x;
    int64_t pen_y = command->y;
    while (offset < command->frame) {
        const uint8_t glyph = next_glyph(bytes, command->frame, &offset);
        if (glyph == (uint8_t)'\n') {
            pen_x = command->x;
            pen_y += P4_CP437_GLYPH_HEIGHT;
            continue;
        }
        if (pen_x < P4_SCRIPT_SCREEN_WIDTH &&
            pen_x + P4_CP437_GLYPH_WIDTH > 0 &&
            pen_y < P4_SCRIPT_SCREEN_HEIGHT &&
            pen_y + P4_CP437_GLYPH_HEIGHT > 0) {
            const size_t glyph_offset =
                (size_t)glyph * P4_CP437_GLYPH_HEIGHT;
            for (int32_t row = 0; row < P4_CP437_GLYPH_HEIGHT; ++row) {
                const uint8_t bits = p4_cp437_font_8x16[
                    glyph_offset + (size_t)row];
                for (int32_t column = 0;
                     column < P4_CP437_GLYPH_WIDTH; ++column) {
                    if ((bits & (uint8_t)(0x80U >> column)) != 0U) {
                        pixel(renderer, (int32_t)pen_x + column,
                              (int32_t)pen_y + row,
                              command->color_rgb565);
                        if (renderer->stats.pixel_budget_exhausted) {
                            return false;
                        }
                    }
                }
            }
        }
        pen_x += P4_CP437_GLYPH_WIDTH;
        if (pen_x > INT32_MAX) {
            break;
        }
    }
    return true;
}

static bool missing_sprite(renderer_t *renderer,
                           const p4_script_render_command_t *command)
{
    const uint16_t first = (command->asset_id & 1U) != 0U
        ? UINT16_C(0xf81f) : UINT16_C(0xffff);
    const uint16_t second = first == UINT16_C(0xf81f)
        ? UINT16_C(0xffff) : UINT16_C(0xf81f);
    for (int32_t row = 0; row < P4_SCRIPT_MISSING_SPRITE_SIZE; ++row) {
        for (int32_t column = 0;
             column < P4_SCRIPT_MISSING_SPRITE_SIZE; ++column) {
            const uint16_t color = ((row / 4 + column / 4) & 1) == 0
                ? first : second;
            pixel(renderer, command->x + column, command->y + row, color);
            if (renderer->stats.pixel_budget_exhausted) return false;
        }
    }
    return true;
}

p4_script_status_t p4_script_render_rgb565(
    const p4_script_render_packet_t *packet,
    uint32_t expected_generation,
    p4_script_surface_t *surface,
    const p4_script_render_services_t *services,
    p4_script_render_stats_t *stats_out)
{
    if (!surface_valid(surface) || stats_out == NULL) {
        return P4_SCRIPT_STATUS_INVALID_ARGUMENT;
    }
    memset(stats_out, 0, sizeof(*stats_out));
    const p4_script_status_t validation =
        p4_render_packet_validate(packet, expected_generation);
    if (validation != P4_SCRIPT_STATUS_OK) {
        return validation;
    }
    renderer_t renderer = {
        .surface = surface,
        .pixels_remaining = P4_SCRIPT_RENDER_PIXEL_BUDGET,
    };
    for (uint32_t index = 0U; index < packet->command_count; ++index) {
        const p4_script_render_command_t *command = &packet->commands[index];
        bool rendered = true;
        switch ((p4_script_render_command_type_t)command->type) {
        case P4_SCRIPT_RENDER_COMMAND_CLEAR:
            rendered = fill_rect(
                &renderer, 0, 0, P4_SCRIPT_SCREEN_WIDTH,
                P4_SCRIPT_SCREEN_HEIGHT, command->color_rgb565);
            break;
        case P4_SCRIPT_RENDER_COMMAND_RECT:
            rendered = (command->flags & P4_SCRIPT_RENDER_FLAG_FILLED) != 0U
                ? fill_rect(&renderer, command->x, command->y,
                            command->width, command->height,
                            command->color_rgb565)
                : outline_rect(&renderer, command);
            break;
        case P4_SCRIPT_RENDER_COMMAND_LINE:
            rendered = line(&renderer, command);
            break;
        case P4_SCRIPT_RENDER_COMMAND_CIRCLE:
            rendered = circle(&renderer, command);
            break;
        case P4_SCRIPT_RENDER_COMMAND_TEXT:
            rendered = text(&renderer, packet, command);
            break;
        case P4_SCRIPT_RENDER_COMMAND_SPRITE:
            if (services != NULL && services->draw_sprite != NULL) {
                rendered = services->draw_sprite(
                    services->context, surface, command->asset_id,
                    command->x, command->y, command->frame, command->flags);
            } else {
                renderer.stats.unsupported_sprites++;
                rendered = missing_sprite(&renderer, command);
            }
            break;
        default:
            rendered = false;
            break;
        }
        if (!rendered) {
            renderer.stats.commands_dropped++;
        } else {
            renderer.stats.commands_rendered++;
        }
        if (renderer.stats.pixel_budget_exhausted) {
            renderer.stats.commands_dropped +=
                packet->command_count - index - 1U;
            break;
        }
    }
    *stats_out = renderer.stats;
    return P4_SCRIPT_STATUS_OK;
}
