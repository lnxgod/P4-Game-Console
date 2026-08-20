// SPDX-License-Identifier: MIT

#ifndef P4_GAME_API_VISUAL_H
#define P4_GAME_API_VISUAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/game.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Signed Q16.16 coordinates keep slow movement smooth without floating point. */
typedef int32_t p4_q16_t;

#define P4_Q16_ONE INT32_C(65536)

p4_q16_t p4_q16_from_int(int32_t value);
int32_t p4_q16_to_int_round(p4_q16_t value);

/** Advance position by a per-second velocity using a bounded frame delta. */
p4_q16_t p4_q16_step(p4_q16_t position,
                     p4_q16_t velocity_per_second,
                     uint32_t elapsed_ms);

/** Smoothstep easing. Both input and output span the complete 0..65535 range. */
uint16_t p4_ease_smoothstep_u16(uint16_t progress);

/** Select a looping or ping-pong sprite frame from elapsed game time. */
size_t p4_animation_frame(uint32_t elapsed_ms,
                          uint32_t frame_duration_ms,
                          size_t frame_count,
                          bool ping_pong);

/** Deterministic bounded camera shake; amplitude is clamped to 32 pixels. */
void p4_camera_shake(uint32_t frame_index, uint32_t seed, int amplitude,
                     int *offset_x, int *offset_y);

typedef enum {
    P4_SPRITE_FLIP_NONE = 0,
    P4_SPRITE_FLIP_X = UINT8_C(1) << 0U,
    P4_SPRITE_FLIP_Y = UINT8_C(1) << 1U,
} p4_sprite_flip_t;

/** One clipped frame inside an RGB565 sprite sheet. No allocation is used. */
typedef struct {
    const uint16_t *pixels;
    size_t sheet_width;
    size_t sheet_height;
    size_t stride_pixels;
    size_t source_x;
    size_t source_y;
    size_t width;
    size_t height;
    uint16_t transparent_color;
    uint8_t scale;
    uint8_t flip;
    bool use_transparency;
} p4_sprite_t;

bool p4_sprite_valid(const p4_sprite_t *sprite);
void p4_draw_sprite(p4_game_surface_t *surface, int x, int y,
                    const p4_sprite_t *sprite);

/** Tiny caller-owned particle; games choose the array size and never allocate. */
typedef struct {
    p4_q16_t x;
    p4_q16_t y;
    p4_q16_t velocity_x;
    p4_q16_t velocity_y;
    uint16_t age_ms;
    uint16_t lifetime_ms;
    uint16_t color;
    uint8_t size;
    bool active;
} p4_particle_t;

void p4_particle_spawn(p4_particle_t *particle,
                       p4_q16_t x, p4_q16_t y,
                       p4_q16_t velocity_x, p4_q16_t velocity_y,
                       uint16_t lifetime_ms, uint16_t color, uint8_t size);
void p4_particles_update(p4_particle_t *particles, size_t count,
                         uint32_t elapsed_ms,
                         p4_q16_t gravity_per_second);
void p4_particles_draw(p4_game_surface_t *surface,
                       const p4_particle_t *particles, size_t count,
                       int camera_x, int camera_y);

#ifdef __cplusplus
}
#endif

#endif
