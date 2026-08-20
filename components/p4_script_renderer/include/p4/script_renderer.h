// SPDX-License-Identifier: MIT

#ifndef P4_SCRIPT_RENDERER_H
#define P4_SCRIPT_RENDERER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/runtime_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t *pixels;
    size_t stride_pixels;
    uint16_t width;
    uint16_t height;
} p4_script_surface_t;

typedef bool (*p4_script_sprite_draw_fn)(
    void *context,
    p4_script_surface_t *surface,
    p4_script_asset_id_t asset_id,
    int32_t x,
    int32_t y,
    uint32_t frame,
    uint8_t flags);

typedef struct {
    void *context;
    p4_script_sprite_draw_fn draw_sprite;
} p4_script_render_services_t;

typedef struct {
    uint32_t commands_rendered;
    uint32_t commands_dropped;
    uint32_t unsupported_sprites;
    uint32_t pixel_budget_used;
    bool pixel_budget_exhausted;
} p4_script_render_stats_t;

/**
 * Replay one validated command packet into a native 768x480 RGB565 surface.
 *
 * Geometry is clipped before rasterization and a fixed per-frame pixel budget
 * prevents hostile carts from turning a bounded command list into unbounded
 * render work. Missing sprite services produce a small deterministic marker;
 * code-drawn games remain fully functional without an asset decoder.
 */
p4_script_status_t p4_script_render_rgb565(
    const p4_script_render_packet_t *packet,
    uint32_t expected_generation,
    p4_script_surface_t *surface,
    const p4_script_render_services_t *services,
    p4_script_render_stats_t *stats_out);

#ifdef __cplusplus
}
#endif

#endif
