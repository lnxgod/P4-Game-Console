// SPDX-License-Identifier: MIT

#include "p4/visual.h"

#include <limits.h>

#include "p4/draw.h"

enum {
    P4_VISUAL_MAX_SPRITE_SIDE = 4096,
    P4_VISUAL_MAX_SPRITE_SCALE = 8,
    P4_VISUAL_MAX_SPRITE_DRAW_PIXELS =
        P4_GAME_SURFACE_HIGH_RES_WIDTH *
        P4_GAME_SURFACE_HIGH_RES_HEIGHT * 4,
    P4_VISUAL_MAX_PARTICLES = 256,
    P4_VISUAL_MAX_SHAKE = 32,
};

p4_q16_t p4_q16_from_int(int32_t value)
{
    if (value > INT32_MAX / P4_Q16_ONE) {
        return INT32_MAX;
    }
    if (value < INT32_MIN / P4_Q16_ONE) {
        return INT32_MIN;
    }
    return value * P4_Q16_ONE;
}

int32_t p4_q16_to_int_round(p4_q16_t value)
{
    if (value >= 0) {
        return (int32_t)(((uint32_t)value + UINT32_C(32768)) >> 16U);
    }
    const uint32_t magnitude = UINT32_C(0) - (uint32_t)value;
    const uint32_t rounded = (magnitude + UINT32_C(32768)) >> 16U;
    return -(int32_t)rounded;
}

p4_q16_t p4_q16_step(p4_q16_t position,
                     p4_q16_t velocity_per_second,
                     uint32_t elapsed_ms)
{
    const uint32_t bounded_ms = elapsed_ms > P4_GAME_MAX_FRAME_DELTA_MS
        ? P4_GAME_MAX_FRAME_DELTA_MS : elapsed_ms;
    const int32_t whole = velocity_per_second / INT32_C(1000);
    const int32_t remainder = velocity_per_second % INT32_C(1000);
    const int32_t delta = whole * (int32_t)bounded_ms +
        remainder * (int32_t)bounded_ms / INT32_C(1000);
    if (delta > 0 && position > INT32_MAX - delta) {
        return INT32_MAX;
    }
    if (delta < 0 && position < INT32_MIN - delta) {
        return INT32_MIN;
    }
    return position + delta;
}

uint16_t p4_ease_smoothstep_u16(uint16_t progress)
{
    const uint32_t maximum = UINT16_MAX;
    const uint32_t value = progress;
    const uint32_t squared =
        (value * value + maximum / 2U) / maximum;
    const uint32_t cubed =
        (squared * value + maximum / 2U) / maximum;
    uint32_t result = 3U * squared - 2U * cubed;
    if (result > maximum) {
        result = maximum;
    }
    return (uint16_t)result;
}

size_t p4_animation_frame(uint32_t elapsed_ms,
                          uint32_t frame_duration_ms,
                          size_t frame_count,
                          bool ping_pong)
{
    if (frame_duration_ms == 0U || frame_count == 0U ||
        frame_count > 1024U) {
        return 0U;
    }
    const uint32_t step = elapsed_ms / frame_duration_ms;
    if (!ping_pong || frame_count == 1U) {
        return (size_t)(step % frame_count);
    }
    const size_t period = frame_count * 2U - 2U;
    const size_t phase = (size_t)(step % period);
    return phase < frame_count ? phase : period - phase;
}

static uint32_t mix_u32(uint32_t value)
{
    value ^= value >> 16U;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15U;
    value *= UINT32_C(0x846ca68b);
    value ^= value >> 16U;
    return value;
}

void p4_camera_shake(uint32_t frame_index, uint32_t seed, int amplitude,
                     int *offset_x, int *offset_y)
{
    if (offset_x == NULL || offset_y == NULL) {
        return;
    }
    if (amplitude == INT_MIN) {
        amplitude = P4_VISUAL_MAX_SHAKE;
    } else if (amplitude < 0) {
        amplitude = -amplitude;
    }
    if (amplitude > P4_VISUAL_MAX_SHAKE) {
        amplitude = P4_VISUAL_MAX_SHAKE;
    }
    if (amplitude == 0) {
        *offset_x = 0;
        *offset_y = 0;
        return;
    }
    const uint32_t span = (uint32_t)(amplitude * 2 + 1);
    const uint32_t first = mix_u32(seed ^ frame_index);
    const uint32_t second = mix_u32(first ^ UINT32_C(0x9e3779b9));
    *offset_x = (int)(first % span) - amplitude;
    *offset_y = (int)(second % span) - amplitude;
}

bool p4_sprite_valid(const p4_sprite_t *sprite)
{
    const uint8_t known_flip = P4_SPRITE_FLIP_X | P4_SPRITE_FLIP_Y;
    if (!(sprite != NULL && sprite->pixels != NULL &&
        sprite->sheet_width > 0U && sprite->sheet_height > 0U &&
        sprite->sheet_width <= P4_VISUAL_MAX_SPRITE_SIDE &&
        sprite->sheet_height <= P4_VISUAL_MAX_SPRITE_SIDE &&
        sprite->stride_pixels >= sprite->sheet_width &&
        sprite->stride_pixels <= P4_VISUAL_MAX_SPRITE_SIDE &&
        sprite->width > 0U && sprite->height > 0U &&
        sprite->width <= sprite->sheet_width &&
        sprite->height <= sprite->sheet_height &&
        sprite->source_x <= sprite->sheet_width - sprite->width &&
        sprite->source_y <= sprite->sheet_height - sprite->height &&
        sprite->scale > 0U && sprite->scale <= P4_VISUAL_MAX_SPRITE_SCALE &&
        (sprite->flip & (uint8_t)~known_flip) == 0U)) {
        return false;
    }
    const size_t source_pixels = sprite->width * sprite->height;
    const size_t scale_pixels = (size_t)sprite->scale * sprite->scale;
    return source_pixels <=
        (size_t)P4_VISUAL_MAX_SPRITE_DRAW_PIXELS / scale_pixels;
}

void p4_draw_sprite(p4_game_surface_t *surface, int x, int y,
                    const p4_sprite_t *sprite)
{
    if (!p4_surface_valid(surface) || !p4_sprite_valid(sprite)) {
        return;
    }
    const int scale = sprite->scale;
    const int64_t right = (int64_t)x + (int64_t)sprite->width * scale;
    const int64_t bottom = (int64_t)y + (int64_t)sprite->height * scale;
    if (right <= 0 || bottom <= 0 || x >= surface->width || y >= surface->height) return;
    /* An intersecting sprite bounds x/y to +/-32768 before narrowing.
     * Preserve source-pixel block order for aliased source/destination. */
    const size_t first_x = x < 0 ? (size_t)(-x / scale) : 0U;
    const size_t first_y = y < 0 ? (size_t)(-y / scale) : 0U;
    const size_t end_x = right > surface->width
        ? (size_t)(((int)surface->width - x + scale - 1) / scale) : sprite->width;
    const size_t end_y = bottom > surface->height
        ? (size_t)(((int)surface->height - y + scale - 1) / scale) : sprite->height;
    for (size_t row = first_y; row < end_y; ++row) {
        const size_t source_row = (sprite->flip & P4_SPRITE_FLIP_Y) != 0U
            ? sprite->height - row - 1U : row;
        const uint16_t *src = sprite->pixels +
            (sprite->source_y + source_row) * sprite->stride_pixels + sprite->source_x;
        int top = y + (int)row * scale, row_end = top + scale;
        if (top < 0) top = 0;
        if (row_end > surface->height) row_end = surface->height;
        for (size_t column = first_x; column < end_x; ++column) {
            const size_t source_column = (sprite->flip & P4_SPRITE_FLIP_X) != 0U
                ? sprite->width - column - 1U : column;
            const uint16_t color = src[source_column];
            if (sprite->use_transparency && color == sprite->transparent_color) continue;
            int left = x + (int)column * scale, column_end = left + scale;
            if (left < 0) left = 0;
            if (column_end > surface->width) column_end = surface->width;
            for (int dy = top; dy < row_end; ++dy) {
                uint16_t *dst = surface->pixels + (size_t)dy * surface->stride_pixels + (size_t)left;
                uint16_t *const end = dst + (size_t)(column_end - left);
                do { *dst++ = color; } while (dst != end);
            }
        }
    }
}

void p4_particle_spawn(p4_particle_t *particle,
                       p4_q16_t x, p4_q16_t y,
                       p4_q16_t velocity_x, p4_q16_t velocity_y,
                       uint16_t lifetime_ms, uint16_t color, uint8_t size)
{
    if (particle == NULL) {
        return;
    }
    *particle = (p4_particle_t){
        .x = x,
        .y = y,
        .velocity_x = velocity_x,
        .velocity_y = velocity_y,
        .lifetime_ms = lifetime_ms,
        .color = color,
        .size = size > 4U ? 4U : size,
        .active = lifetime_ms != 0U && size != 0U,
    };
}

void p4_particles_update(p4_particle_t *particles, size_t count,
                         uint32_t elapsed_ms,
                         p4_q16_t gravity_per_second)
{
    if (particles == NULL || count > P4_VISUAL_MAX_PARTICLES) {
        return;
    }
    const uint32_t bounded_ms = elapsed_ms > P4_GAME_MAX_FRAME_DELTA_MS
        ? P4_GAME_MAX_FRAME_DELTA_MS : elapsed_ms;
    for (size_t index = 0U; index < count; ++index) {
        p4_particle_t *const particle = &particles[index];
        if (!particle->active) {
            continue;
        }
        const uint32_t age = (uint32_t)particle->age_ms + bounded_ms;
        if (age >= particle->lifetime_ms) {
            particle->age_ms = particle->lifetime_ms;
            particle->active = false;
            continue;
        }
        particle->age_ms = (uint16_t)age;
        particle->velocity_y = p4_q16_step(
            particle->velocity_y, gravity_per_second, bounded_ms);
        particle->x = p4_q16_step(
            particle->x, particle->velocity_x, bounded_ms);
        particle->y = p4_q16_step(
            particle->y, particle->velocity_y, bounded_ms);
    }
}

void p4_particles_draw(p4_game_surface_t *surface,
                       const p4_particle_t *particles, size_t count,
                       int camera_x, int camera_y)
{
    if (!p4_surface_valid(surface) || particles == NULL ||
        count > P4_VISUAL_MAX_PARTICLES) {
        return;
    }
    for (size_t index = 0U; index < count; ++index) {
        const p4_particle_t *const particle = &particles[index];
        if (!particle->active) {
            continue;
        }
        p4_draw_fill_rect(
            surface, p4_q16_to_int_round(particle->x) - camera_x,
            p4_q16_to_int_round(particle->y) - camera_y,
            particle->size, particle->size, particle->color);
    }
}
