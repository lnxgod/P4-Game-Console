// SPDX-License-Identifier: MIT

#include "p4/input.h"

#include <string.h>

#include "p4/draw.h"
#include "p4/presentation.h"

void p4_game_input_mapper_init(p4_game_input_mapper_t *mapper)
{
    if (mapper != NULL) {
        *mapper = (p4_game_input_mapper_t){0};
    }
}

bool p4_game_map_physical_touch(uint16_t physical_x,
                                uint16_t physical_y,
                                p4_game_point_t *out_logical)
{
    /* A point before an offset wraps past the bounded viewport, which keeps
     * this mapping valid when an offset is zero for the 10 in panel. */
    const uint16_t relative_x =
        (uint16_t)(physical_x - P4_INPUT_VIEWPORT_LEFT);
    const uint16_t relative_y =
        (uint16_t)(physical_y - P4_INPUT_VIEWPORT_TOP);
    if (out_logical == NULL ||
        relative_x >= P4_INPUT_VIEWPORT_WIDTH ||
        relative_y >= P4_INPUT_VIEWPORT_HEIGHT) {
        return false;
    }
    out_logical->x = (uint16_t)(
        ((uint32_t)relative_x * P4_GAME_SURFACE_WIDTH) /
        P4_INPUT_VIEWPORT_WIDTH);
    out_logical->y = (uint16_t)(
        ((uint32_t)relative_y * P4_GAME_SURFACE_HEIGHT) /
        P4_INPUT_VIEWPORT_HEIGHT);
    return out_logical->x < P4_GAME_SURFACE_WIDTH &&
        out_logical->y < P4_GAME_SURFACE_HEIGHT;
}

static bool point_in_rect(const p4_game_point_t *point,
                          unsigned left, unsigned top,
                          unsigned width, unsigned height)
{
    const unsigned x = point->x;
    const unsigned y = point->y;
    return x >= left && x < left + width &&
        y >= top && y < top + height;
}

static bool point_in_circle(const p4_game_point_t *point,
                            int center_x, int center_y, int radius)
{
    const int x = (int)point->x - center_x;
    const int y = (int)point->y - center_y;
    return x * x + y * y <= radius * radius;
}

static uint32_t buttons_for_point(const p4_game_point_t *point)
{
    uint32_t buttons = 0U;
    if (point_in_rect(point, 0U, 0U, 52U, 24U)) {
        buttons |= P4_BUTTON_BACK;
    }
    if (point_in_rect(point, 268U, 0U, 52U, 24U)) {
        buttons |= P4_BUTTON_START;
    }
    if (point_in_rect(point, 34U, 132U, 24U, 26U)) {
        buttons |= P4_BUTTON_UP;
    }
    if (point_in_rect(point, 34U, 174U, 24U, 26U)) {
        buttons |= P4_BUTTON_DOWN;
    }
    if (point_in_rect(point, 8U, 158U, 26U, 25U)) {
        buttons |= P4_BUTTON_LEFT;
    }
    if (point_in_rect(point, 58U, 158U, 26U, 25U)) {
        buttons |= P4_BUTTON_RIGHT;
    }
    if (point_in_circle(point, 286, 158, 22)) {
        buttons |= P4_BUTTON_A;
    }
    if (point_in_circle(point, 240, 176, 18)) {
        buttons |= P4_BUTTON_B;
    }
    return buttons;
}

void p4_game_input_mapper_update(
    p4_game_input_mapper_t *mapper,
    bool touch_valid,
    const p4_physical_touch_t *touches,
    size_t touch_count,
    uint32_t digital_buttons,
    p4_game_input_t *out_input)
{
    if (mapper == NULL || out_input == NULL) {
        return;
    }
    *out_input = (p4_game_input_t){0};
    uint32_t held = digital_buttons & P4_BUTTON_MASK;
    const bool snapshot_valid = touch_valid &&
        touch_count <= P4_INPUT_MAX_TOUCHES &&
        (touch_count == 0U || touches != NULL);
    out_input->touch_valid = snapshot_valid;
    if (snapshot_valid) {
        for (size_t i = 0U; i < touch_count; ++i) {
            p4_game_point_t logical;
            if (!p4_game_map_physical_touch(
                    touches[i].x, touches[i].y, &logical)) {
                continue;
            }
            if (out_input->touch_count < P4_INPUT_MAX_TOUCHES) {
                out_input->touches[out_input->touch_count] = logical;
                ++out_input->touch_count;
            }
            held |= buttons_for_point(&logical);
        }
    }
    out_input->held = held;
    out_input->pressed = held & ~mapper->previous_held;
    out_input->released = mapper->previous_held & ~held;
    mapper->previous_held = held;
}

static uint16_t choose_color(bool active,
                             uint16_t color,
                             uint16_t active_color)
{
    return active ? active_color : color;
}

static int control_x(const p4_game_surface_t *surface, int value)
{
    return value * (int)surface->width / P4_GAME_SURFACE_WIDTH;
}

static int control_y(const p4_game_surface_t *surface, int value)
{
    return value * (int)surface->height / P4_GAME_SURFACE_HEIGHT;
}

static void draw_control_fill(p4_game_surface_t *surface, int x, int y,
                              int width, int height, uint16_t color)
{
    if (surface->width != P4_GAME_SURFACE_HIGH_RES_WIDTH) {
        p4_draw_fill_rect(surface, x, y, width, height, color);
        return;
    }
    for (int row = 0; row < height; ++row) {
        if (y + row < 0 || y + row >= (int)surface->height) continue;
        for (int col = 0; col < width; ++col) {
            if (x + col < 0 || x + col >= (int)surface->width) continue;
            uint16_t *pixel = &surface->pixels[(size_t)(y + row) * surface->stride_pixels + (size_t)(x + col)];
            *pixel = p4_ui_blend(*pixel, color, 4U);
        }
    }
    p4_draw_rect(surface, x, y, width, height, color);
    p4_draw_rect(surface, x + 1, y + 1, width - 2, height - 2, color);
}
static void draw_control_disc(p4_game_surface_t *surface, int x, int y,
                              int radius, uint16_t color)
{
    if (surface->width != P4_GAME_SURFACE_HIGH_RES_WIDTH) {
        p4_draw_fill_circle(surface, x, y, radius, color);
        return;
    }
    const int inner = (radius - 2) * (radius - 2);
    for (int row = -radius; row <= radius; ++row) {
        if (y + row < 0 || y + row >= (int)surface->height) continue;
        for (int col = -radius; col <= radius; ++col) {
            if (x + col < 0 || x + col >= (int)surface->width) continue;
            const int distance = row * row + col * col;
            if (distance > radius * radius) continue;
            uint16_t *pixel = &surface->pixels[(size_t)(y + row) * surface->stride_pixels + (size_t)(x + col)];
            *pixel = p4_ui_blend(*pixel, color, distance >= inner ? 15U : 4U);
        }
    }
}

static void draw_control_label(p4_game_surface_t *surface, int x, int y,
                               const char *text, uint16_t color,
                               unsigned scale, size_t limit)
{
    if (surface->width == P4_GAME_SURFACE_HIGH_RES_WIDTH) {
        const uint16_t ink = color == 0U ? UINT16_C(0xffff) : color;
        p4_ui_text(surface, x + 1, y - 3, text, 0U, 24U, limit);
        p4_ui_text(surface, x, y - 4, text, ink, 24U, limit);
    } else {
        p4_draw_text(surface, x, y, text, color, scale, limit);
    }
}

void p4_game_draw_standard_controls(p4_game_surface_t *surface,
                                    uint16_t color,
                                    uint16_t active_color,
                                    uint32_t held_buttons)
{
    if (!p4_surface_valid(surface)) {
        return;
    }
    const unsigned text_scale =
        surface->width == P4_GAME_SURFACE_HIGH_RES_WIDTH ? 2U : 1U;
    p4_draw_rect(surface, 0, 0,
                 control_x(surface, 52), control_y(surface, 24),
                 choose_color((held_buttons & P4_BUTTON_BACK) != 0U,
                              color, active_color));
    draw_control_label(surface, control_x(surface, 8), control_y(surface, 8),
                 "EXIT",
                 choose_color((held_buttons & P4_BUTTON_BACK) != 0U,
                              color, active_color), text_scale, 4U);
    p4_draw_rect(surface, control_x(surface, 268), 0,
                 control_x(surface, 52), control_y(surface, 24),
                 choose_color((held_buttons & P4_BUTTON_START) != 0U,
                              color, active_color));
    draw_control_label(surface, control_x(surface, 276), control_y(surface, 8),
                 "START",
                 choose_color((held_buttons & P4_BUTTON_START) != 0U,
                              color, active_color), text_scale, 5U);

    draw_control_fill(surface, control_x(surface, 34),
                      control_y(surface, 132),
                      control_x(surface, 24), control_y(surface, 26),
                      choose_color((held_buttons & P4_BUTTON_UP) != 0U,
                                   color, active_color));
    draw_control_fill(surface, control_x(surface, 34),
                      control_y(surface, 174),
                      control_x(surface, 24), control_y(surface, 26),
                      choose_color((held_buttons & P4_BUTTON_DOWN) != 0U,
                                   color, active_color));
    draw_control_fill(surface, control_x(surface, 8),
                      control_y(surface, 158),
                      control_x(surface, 26), control_y(surface, 25),
                      choose_color((held_buttons & P4_BUTTON_LEFT) != 0U,
                                   color, active_color));
    draw_control_fill(surface, control_x(surface, 58),
                      control_y(surface, 158),
                      control_x(surface, 26), control_y(surface, 25),
                      choose_color((held_buttons & P4_BUTTON_RIGHT) != 0U,
                                   color, active_color));
    draw_control_label(surface, control_x(surface, 43), control_y(surface, 142),
                 "U", UINT16_C(0x0000), text_scale, 1U);
    draw_control_label(surface, control_x(surface, 43), control_y(surface, 184),
                 "D", UINT16_C(0x0000), text_scale, 1U);
    draw_control_label(surface, control_x(surface, 18), control_y(surface, 168),
                 "L", UINT16_C(0x0000), text_scale, 1U);
    draw_control_label(surface, control_x(surface, 68), control_y(surface, 168),
                 "R", UINT16_C(0x0000), text_scale, 1U);

    draw_control_disc(surface, control_x(surface, 286),
                        control_y(surface, 158), control_x(surface, 22),
                        choose_color((held_buttons & P4_BUTTON_A) != 0U,
                                     color, active_color));
    draw_control_disc(surface, control_x(surface, 240),
                        control_y(surface, 176), control_x(surface, 18),
                        choose_color((held_buttons & P4_BUTTON_B) != 0U,
                                     color, active_color));
    draw_control_label(surface, control_x(surface, 283), control_y(surface, 155),
                 "A", UINT16_C(0x0000), text_scale, 1U);
    draw_control_label(surface, control_x(surface, 237), control_y(surface, 173),
                 "B", UINT16_C(0x0000), text_scale, 1U);
}
