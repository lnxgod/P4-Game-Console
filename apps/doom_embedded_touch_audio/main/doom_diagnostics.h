// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef P4_DOOM_DIAGNOSTICS_H
#define P4_DOOM_DIAGNOSTICS_H
#include "diag_mailbox.h"
#include "doom/audio_runtime.h"
#include "platform/audio.h"
#include "platform/display.h"
#include "p4_doom_net.h"
typedef enum {
    DOOM_PERF_INTERVAL,
    DOOM_PERF_DG,
    DOOM_PERF_COMPOSE,
    DOOM_PERF_SUBMIT,
    DOOM_PERF_NET,
    DOOM_PERF_DEBUG,
    DOOM_PERF_TOUCH,
    DOOM_PERF_GAMEPAD,
    DOOM_PERF_SLEEP,
    DOOM_PERF_LOG,
    DOOM_PERF_COUNT
} doom_perf_phase_t;

typedef struct {
    uint64_t total_us;
    uint64_t max_us;
    uint32_t calls;
} doom_perf_sample_t;

typedef struct {
    doom_perf_sample_t phases[DOOM_PERF_COUNT];
    doom_perf_sample_t engine[P4_DOOM_ENGINE_PHASE_COUNT];
    uint64_t started_us;
    uint64_t previous_frame_us;
    uint64_t dg_started_us;
    uint32_t frames;
    uint32_t dg_depth;
    bool active;
} doom_perf_state_t;

/* Engine-owner copy only. captured_us/age describe the last copied observation;
 * active describes handle ownership at the enclosing diagnostic snapshot.
 * Retained observations after stop are not claimed to be final worker totals. */
typedef struct {
    uint64_t captured_us, samples, poll_calls, poll_total_us, age_us;
    int64_t last_completed_us;
    uint32_t epoch, poll_max_us, overflows, stale_neutralizations;
    uint32_t producer_stalls, recoveries;
    int32_t fault;
    bool present, active, stats_valid, age_valid;
} doom_touch_sampler_diag_t;

typedef enum {
    DOOM_MEMORY_ENGINE_START,
    DOOM_MEMORY_PRE_ZONE,
    DOOM_MEMORY_POST_ZONE,
    DOOM_MEMORY_ENGINE_READY,
    DOOM_MEMORY_FIRST_MAP,
    DOOM_MEMORY_CHECKPOINT_CAPTURE,
    DOOM_MEMORY_CHECKPOINT_RESTORE,
    DOOM_MEMORY_RUNTIME,
    DOOM_MEMORY_PHASE_COUNT
} doom_memory_phase_t;

/* Scalar owner observations. Event hooks mark time only; observed_us may be
 * later, after the engine call returns. boot_min_free is the SDK's sum of
 * per-region lifetime minima, not a simultaneous or game-local minimum. */
typedef struct {
    uint64_t event_us, observed_us, free_bytes, largest_bytes, boot_min_free;
} doom_memory_observation_t;
typedef struct {
    doom_memory_observation_t phases[DOOM_MEMORY_PHASE_COUNT];
    uint64_t sampled_since_us, sampled_min_free;
    uint64_t zone_requested_bytes, arena_resident_bytes;
    uint32_t sample_count, valid_mask, zone_allocation_attempts;
    int32_t zone_admission_error;
    bool present, zone_allocation_succeeded;
} doom_memory_diag_t;

typedef struct {
    uint64_t captured_us;
    doom_perf_state_t perf;
    platform_display_stats_t display;
    doom_audio_runtime_stats_t audio;
    platform_audio_adapter_stats_t adapter;
    platform_audio_telemetry_t backend;
    p4_doom_net_stats_t net;
    doom_touch_sampler_diag_t touch_sampler;
    doom_memory_diag_t memory;
    uint32_t frame_count, touch_polls, touch_failures, touch_retries;
    bool composite_gate, touch_gate, audio_gate;
    bool display_valid, audio_valid, backend_valid;
} doom_diag_snapshot_t;
_Static_assert(sizeof(doom_diag_snapshot_t) <= P4_DIAG_PAYLOAD_BYTES,
               "Doom diagnostic snapshot exceeds its fixed slot");
/* Sole engine owner only. Begin may create one persistent low-priority task.
 * Reserve/publish/end never format, log, allocate, wait, or call collectors. */
void doom_diagnostics_begin(void);
void doom_diagnostics_end(void);
bool doom_diagnostics_reserve(p4_diag_reservation_t *reservation);
void doom_diagnostics_publish(p4_diag_reservation_t reservation,
                              const doom_diag_snapshot_t *snapshot);
#endif
