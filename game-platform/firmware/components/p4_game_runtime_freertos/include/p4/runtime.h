#ifndef P4_RUNTIME_H
#define P4_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "p4/runtime_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define P4_CARTRIDGE_HASH_BYTES 32U

typedef struct {
    uint8_t sha256[P4_CARTRIDGE_HASH_BYTES];
    uint32_t generation;
    uint32_t game_api_version;
    uint32_t code_bytes;
    uint32_t asset_bytes;
    uint32_t storage_id;
} p4_cartridge_ref_t;

typedef struct {
    void *context;
    /* Trusted platform callback; must return promptly and never block indefinitely. */
    p4_status_t (*load)(void *context, const p4_cartridge_ref_t *cartridge);
    /* Trusted platform callback; must return promptly and never block indefinitely. */
    p4_status_t (*tick)(
        void *context,
        const p4_tick_frame_t *tick,
        p4_render_writer_t *render);
    /*
     * May run concurrently with load/tick. It must only signal a trusted VM
     * backend, return promptly, and never tear down backend state.
     */
    void (*request_interrupt)(void *context);
    /* Trusted platform callback; must return promptly and never block indefinitely. */
    p4_status_t (*unload)(void *context);
} p4_cartridge_backend_t;

typedef struct {
    void *context;
    /* Trusted platform callback; must return promptly and publish one complete snapshot. */
    bool (*read)(void *context, p4_raw_input_t *input_out);
} p4_input_source_t;

typedef struct {
    void *context;
    /* Must return within its own bounded device-service timeout. */
    p4_status_t (*submit)(void *context, const p4_render_packet_t *packet);
} p4_render_sink_t;

typedef struct {
    uint32_t tick_hz;
    uint32_t update_soft_budget_us;
    /* Detected after a native callback returns; a VM must enforce its own fuel/interrupt limit. */
    uint32_t update_hard_budget_us;
    uint32_t max_catchup_ticks;
    uint32_t startup_timeout_ms;
    uint32_t stop_timeout_ms;
    uint32_t game_task_priority;
    uint32_t render_task_priority;
    bool subscribe_game_task_watchdog;
} p4_runtime_config_t;

typedef struct {
    uint64_t simulation_ticks;
    uint64_t frames_published;
    uint64_t frames_rendered;
    uint32_t frames_dropped;
    uint32_t render_commands_dropped;
    uint32_t deadline_misses;
    uint32_t schedule_rebases;
    uint32_t soft_budget_overruns;
    uint32_t hard_budget_overruns;
    uint32_t tick_failures;
    uint32_t render_failures;
    uint32_t max_update_us;
    uint32_t game_stack_min_free_bytes;
    uint32_t render_stack_min_free_bytes;
    p4_status_t last_error;
    bool game_watchdog_subscribed;
} p4_runtime_stats_t;

void p4_runtime_default_config(p4_runtime_config_t *config_out);

p4_status_t p4_runtime_start(
    const p4_runtime_config_t *config,
    const p4_cartridge_ref_t *cartridge,
    const p4_cartridge_backend_t *backend,
    const p4_input_source_t *input,
    const p4_render_sink_t *render);

/* Public runtime calls have one serialized supervisor owner. */
p4_status_t p4_runtime_request_stop(uint32_t timeout_ms);
void p4_runtime_get_stats(p4_runtime_stats_t *stats_out);
p4_lifecycle_state_t p4_runtime_get_state(void);

#ifdef __cplusplus
}
#endif

#endif
