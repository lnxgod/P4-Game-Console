#ifndef P4_RUNTIME_CORE_H
#define P4_RUNTIME_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "p4/game_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define P4_RENDER_PACKET_FLAG_OVERFLOW UINT16_C(0x0001)
#define P4_RENDER_SLOT_NONE UINT8_MAX

typedef struct {
    uint64_t phase;
    uint32_t clock_hz;
    uint32_t tick_hz;
} p4_tick_scheduler_t;

p4_status_t p4_tick_scheduler_init(
    p4_tick_scheduler_t *scheduler,
    uint32_t clock_hz,
    uint32_t tick_hz);

p4_status_t p4_tick_scheduler_next(
    p4_tick_scheduler_t *scheduler,
    uint32_t *interval_out);

typedef struct {
    uint64_t previous_down;
    uint32_t source_epoch;
    uint32_t input_epoch;
    bool source_seen;
    bool previous_connected;
    bool neutral_barrier;
} p4_input_latch_t;

void p4_input_latch_init(p4_input_latch_t *latch);
void p4_input_latch_reset(p4_input_latch_t *latch);

void p4_input_latch_sample(
    p4_input_latch_t *latch,
    const p4_raw_input_t *raw,
    uint64_t tick,
    p4_input_frame_t *frame_out);

typedef struct {
    p4_render_packet_t *packet;
    uint32_t dropped_count;
    bool capacity_overflowed;
} p4_render_writer_t;

void p4_render_writer_begin(
    p4_render_writer_t *writer,
    p4_render_packet_t *packet,
    uint32_t generation,
    uint64_t tick);

void p4_render_clear(p4_render_writer_t *writer, uint16_t color_rgb565);

void p4_render_rect(
    p4_render_writer_t *writer,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint16_t color_rgb565);

void p4_render_sprite(
    p4_render_writer_t *writer,
    p4_asset_id_t asset_id,
    int32_t x,
    int32_t y,
    uint32_t frame,
    uint8_t flags);

void p4_render_writer_finish(p4_render_writer_t *writer);
uint32_t p4_render_writer_dropped_count(const p4_render_writer_t *writer);

p4_status_t p4_render_packet_validate(
    const p4_render_packet_t *packet,
    uint32_t expected_generation);

typedef enum {
    P4_RENDER_SLOT_FREE = 0,
    P4_RENDER_SLOT_BUILDING = 1,
    P4_RENDER_SLOT_PENDING = 2,
    P4_RENDER_SLOT_RENDERING = 3
} p4_render_slot_state_t;

typedef struct {
    p4_render_packet_t packets[P4_RENDER_PACKET_COUNT];
    uint8_t states[P4_RENDER_PACKET_COUNT];
    uint8_t pending_slot;
} p4_render_pool_t;

void p4_render_pool_init(p4_render_pool_t *pool);

p4_status_t p4_render_pool_begin(
    p4_render_pool_t *pool,
    uint32_t generation,
    uint64_t tick,
    p4_render_writer_t *writer_out,
    uint8_t *slot_out);

p4_status_t p4_render_pool_publish(p4_render_pool_t *pool, uint8_t slot);
void p4_render_pool_cancel(p4_render_pool_t *pool, uint8_t slot);

p4_status_t p4_render_pool_acquire_latest(
    p4_render_pool_t *pool,
    uint32_t current_generation,
    const p4_render_packet_t **packet_out,
    uint8_t *slot_out);

void p4_render_pool_release(p4_render_pool_t *pool, uint8_t slot);
void p4_render_pool_discard_generation(p4_render_pool_t *pool, uint32_t generation);
bool p4_render_pool_generation_busy(const p4_render_pool_t *pool, uint32_t generation);

typedef enum {
    P4_LIFECYCLE_IDLE = 0,
    P4_LIFECYCLE_LOADING = 1,
    P4_LIFECYCLE_RUNNING = 2,
    P4_LIFECYCLE_STOP_REQUESTED = 3,
    P4_LIFECYCLE_QUIESCED = 4,
    P4_LIFECYCLE_RENDER_DRAINING = 5,
    P4_LIFECYCLE_UNLOADING = 6,
    P4_LIFECYCLE_FAULTED = 7
} p4_lifecycle_state_t;

typedef struct {
    p4_lifecycle_state_t state;
    uint32_t generation;
    p4_status_t last_error;
} p4_lifecycle_t;

void p4_lifecycle_init(p4_lifecycle_t *lifecycle);
p4_status_t p4_lifecycle_begin_load(p4_lifecycle_t *lifecycle, uint32_t generation);
p4_status_t p4_lifecycle_mark_loaded(p4_lifecycle_t *lifecycle);
p4_status_t p4_lifecycle_request_stop(p4_lifecycle_t *lifecycle);
p4_status_t p4_lifecycle_mark_quiesced(p4_lifecycle_t *lifecycle);
p4_status_t p4_lifecycle_begin_render_drain(p4_lifecycle_t *lifecycle);
p4_status_t p4_lifecycle_begin_unload(p4_lifecycle_t *lifecycle);
p4_status_t p4_lifecycle_finish_unload(p4_lifecycle_t *lifecycle);
void p4_lifecycle_fault(p4_lifecycle_t *lifecycle, p4_status_t error);

#ifdef __cplusplus
}
#endif

#endif
