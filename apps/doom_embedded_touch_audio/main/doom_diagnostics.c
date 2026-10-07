// SPDX-License-Identifier: GPL-2.0-or-later
/* Tab5 Console OS periodic diagnostics. One foreground owner, one consumer.
 * Keep this service alive across game visits; transport admission never waits. */
#include "doom_diagnostics.h"
#include "doom/video_metrics.h"
#include "p4/multiplayer_uart.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop

enum { DIAG_PARTS = 13, DIAG_LINE_BYTES = 4096, DIAG_STACK_BYTES = 6144 };
_Static_assert(P4_DOOM_ENGINE_PHASE_COUNT == 10, "DIAG_V2 phase mapping must stay stable");
static p4_diag_mailbox_t s_mailbox;
/* Owner-only initialization flags, never read by the consumer. */
static bool s_initialized, s_task_attempted, s_task_ready;
/* Consumer-only fixed scratch; no borrowed engine state or large stack copy. */
static p4_diag_record_t s_record;
static doom_diag_snapshot_t s_snapshot;
static char s_line[DIAG_LINE_BYTES];
_Static_assert((unsigned)DIAG_LINE_BYTES <= (unsigned)P4_MP_UART_RAW_TX_MAX_BYTES,
               "Complete diagnostic record must fit one endpoint admission");
/* Consumer-only, boot-lifetime counters, independent of mailbox drops. */
static uint32_t s_transport_rejected, s_format_rejected;
static uint32_t s_reported_transport_rejected, s_reported_format_rejected;

static void reject_count(uint32_t *count)
{ if (*count < UINT32_MAX) ++*count; }

static bool append_vformat(size_t *used, const char *format, va_list args)
{
    const size_t available = sizeof(s_line) - *used;
    const int count = vsnprintf(s_line + *used, available, format, args);
    if (count < 0 || (size_t)count >= available) {
        reject_count(&s_format_rejected);
        return false;
    }
    *used += (size_t)count;
    return true;
}

static bool append_format(size_t *, const char *, ...)
    __attribute__((format(printf, 2, 3)));
static bool append_format(size_t *used, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const bool result = append_vformat(used, format, args);
    va_end(args);
    return result;
}

static bool admit_line(size_t length)
{
    const bool admitted = p4_mp_uart_endpoint_try_console_record(
        (const uint8_t *)s_line, length) == ESP_OK;
    if (!admitted) reject_count(&s_transport_rejected);
    /* Preserve the consumer scheduling point after every bounded attempt. */
    vTaskDelay(1U);
    return admitted;
}

static void emit_transport_status(void)
{
    if (s_transport_rejected == s_reported_transport_rejected &&
        s_format_rejected == s_reported_format_rejected) return;
    const uint32_t transport = s_transport_rejected;
    const uint32_t formatting = s_format_rejected;
    size_t used = 0U;
    /* Separate schema: existing DIAG_V2/WORKER_V2 fields remain exact. Only
     * attempt this after a complete ordinary report has been admitted. */
    if (append_format(&used, "\nP4_DOOM DIAG_TRANSPORT_V1"
            " transport_rejected=%" PRIu32 " format_rejected=%" PRIu32
            " emit_us=%" PRIu64 "\n", transport, formatting,
            (uint64_t)esp_timer_get_time()) && admit_line(used)) {
        s_reported_transport_rejected = transport;
        s_reported_format_rejected = formatting;
    }
}

static bool emit_line(const p4_diag_record_t *, const doom_diag_snapshot_t *,
                      const char *, unsigned, const char *, ...)
    __attribute__((format(printf, 5, 6)));

static bool emit_line(const p4_diag_record_t *record,
                      const doom_diag_snapshot_t *snapshot,
                      const char *kind, unsigned part,
                      const char *format, ...)
{
    if (!p4_diag_is_current(&s_mailbox, record)) return false;
    size_t used = 0U;
    if (!append_format(&used, "\nP4_DOOM DIAG_V2 kind=%s ", kind)) return false;
    va_list args;
    va_start(args, format);
    const bool formatted = append_vformat(&used, format, args);
    va_end(args);
    if (!formatted) return false;
    const uint64_t emitted_us = (uint64_t)esp_timer_get_time();
    if (!append_format(&used,
        " diag_schema=2 diag_generation=%" PRIu32 " diag_sequence=%" PRIu64
        " diag_capture_us=%" PRIu64 " diag_emit_us=%" PRIu64
        " diag_part=%u diag_parts=%u diag_dropped=%" PRIu32 " display_valid=%u\n",
        record->generation, record->sequence, record->captured_us,
        emitted_us, part, (unsigned)DIAG_PARTS, record->dropped_full,
        snapshot->display_valid ? 1U : 0U) ||
        !p4_diag_is_current(&s_mailbox, record)) return false;
    /* The leading LF terminates unrelated VFS fragments. Entire records are
     * admitted or rejected, with no suffix retry and no logging on failure. */
    return admit_line(used);
}

static uint64_t doom_perf_average(const doom_diag_snapshot_t *snapshot,
                                  doom_perf_phase_t phase)
{
    const doom_perf_sample_t *const sample = &snapshot->perf.phases[phase];
    return sample->calls == 0U ? 0U : sample->total_us / sample->calls;
}
#define DOOM_PERF_FIELDS(name) \
    " " name "_n=%" PRIu32 " " name "_avg_us=%" PRIu64 \
    " " name "_max_us=%" PRIu64
#define DOOM_PERF_VALUES(phase) \
    snapshot->perf.phases[phase].calls, doom_perf_average(snapshot, phase), \
    snapshot->perf.phases[phase].max_us

static bool emit_engine_performance(const doom_diag_snapshot_t *snapshot,
                                    const p4_diag_record_t *record)
{
    static const char *const names[P4_DOOM_ENGINE_PHASE_COUNT] = {
        "tics", "sim", "sound", "display", "setup", "bsp", "planes",
        "masked", "palette", "net"
    };
    for (unsigned i = 0; i < P4_DOOM_ENGINE_PHASE_COUNT; ++i) {
        const doom_perf_sample_t *const sample = &snapshot->perf.engine[i];
        if (!emit_line(record, snapshot, "ENGINEPERF", i + 3U, "frames=%" PRIu32 " phase=%s"
                 " calls=%" PRIu32 " total_us=%" PRIu64 " max_us=%" PRIu64,
                 snapshot->perf.frames, names[i], sample->calls,
                 sample->total_us, sample->max_us)) return false;
    }
    return true;
}

static bool emit_performance_stats(const doom_diag_snapshot_t *snapshot,
                                   const p4_diag_record_t *record)
{
    const platform_display_stats_t *const display = &snapshot->display;
    if (!emit_line(record, snapshot, "PERF", 2U,
        "frames=%" PRIu32 " elapsed_us=%" PRIu64
        " dg_total_us=%" PRIu64 " log_total_us=%" PRIu64
        DOOM_PERF_FIELDS("frame") DOOM_PERF_FIELDS("dg")
        DOOM_PERF_FIELDS("compose") DOOM_PERF_FIELDS("submit")
        DOOM_PERF_FIELDS("net") DOOM_PERF_FIELDS("debug")
        DOOM_PERF_FIELDS("touch") DOOM_PERF_FIELDS("gamepad")
        DOOM_PERF_FIELDS("sleep") DOOM_PERF_FIELDS("log")
        " log_scope=capture display_reuse_last_us=%" PRIu32 " display_reuse_max_us=%" PRIu32
        " display_transform_last_us=%" PRIu32 " display_transform_max_us=%" PRIu32
        " display_prescale_last_us=%" PRIu32 " display_prescale_max_us=%" PRIu32
        " display_ppa_last_us=%" PRIu32 " display_ppa_max_us=%" PRIu32
        " display_handoff_last_us=%" PRIu32 " display_handoff_max_us=%" PRIu32,
        snapshot->perf.frames, snapshot->captured_us - snapshot->perf.started_us,
        snapshot->perf.phases[DOOM_PERF_DG].total_us,
        snapshot->perf.phases[DOOM_PERF_LOG].total_us,
        DOOM_PERF_VALUES(DOOM_PERF_INTERVAL), DOOM_PERF_VALUES(DOOM_PERF_DG),
        DOOM_PERF_VALUES(DOOM_PERF_COMPOSE), DOOM_PERF_VALUES(DOOM_PERF_SUBMIT),
        DOOM_PERF_VALUES(DOOM_PERF_NET), DOOM_PERF_VALUES(DOOM_PERF_DEBUG),
        DOOM_PERF_VALUES(DOOM_PERF_TOUCH), DOOM_PERF_VALUES(DOOM_PERF_GAMEPAD),
        DOOM_PERF_VALUES(DOOM_PERF_SLEEP), DOOM_PERF_VALUES(DOOM_PERF_LOG),
        display->pipeline_reuse_wait_last_us, display->pipeline_reuse_wait_max_us,
        display->pipeline_transform_last_us, display->pipeline_transform_max_us,
        display->pipeline_prescale_last_us, display->pipeline_prescale_max_us,
        display->pipeline_ppa_last_us, display->pipeline_ppa_max_us,
        display->pipeline_handoff_last_us, display->pipeline_handoff_max_us)) return false;
    if (!emit_engine_performance(snapshot, record)) return false;
    const p4_doom_net_stats_t net = snapshot->net;
    if (!emit_line(record, snapshot, "NETPERF", 13U,
        "frames=%" PRIu32 " poll_calls=%" PRIu32
        " poll_us=%" PRIu64 " poll_max_us=%" PRIu64
        " tx_attempts=%" PRIu32 " tx_failures=%" PRIu32
        " rx_packets=%" PRIu32 " rx_session_rejected=%" PRIu32
        " peer_departures=%" PRIu32 " host_blocked_peer_polls=%" PRIu32
        " rx_poll_calls=%" PRIu32 " rx_poll_skips=%" PRIu32
        " rx_busy_polls=%" PRIu32 " rx_poll_us=%" PRIu64
        " rx_poll_max_us=%" PRIu64 " rx_gap_max_us=%" PRIu64,
        snapshot->perf.frames, net.poll_calls, net.poll_us, net.poll_max_us,
        net.tx_attempts, net.tx_failures, net.rx_packets,
        net.rx_session_rejected, net.peer_departures,
        net.host_blocked_peer_polls, net.rx_poll_calls, net.rx_poll_skips,
        net.rx_busy_polls, net.rx_poll_us, net.rx_poll_max_us, net.rx_gap_max_us)) return false;
    return true;
}

static bool emit_snapshot(const doom_diag_snapshot_t *snapshot,
                          const p4_diag_record_t *record)
{
    const platform_display_stats_t video_stats = snapshot->display;
    const doom_audio_runtime_stats_t audio_stats = snapshot->audio;
    const platform_audio_adapter_stats_t adapter_stats = snapshot->adapter;
    const platform_audio_telemetry_t backend_stats = snapshot->backend;
    const bool have_audio_stats = snapshot->audio_valid;
    const bool have_backend_stats = snapshot->backend_valid;
    const char *const start_proof =
        adapter_stats.running_low_readback_proven_at_start
            ? "low-readback-proven-at-start" : "not-proven";
    if (!emit_line(record, snapshot, "STATS", 1U,
             "frames=%" PRIu32 " submits=%" PRIu32
             " completions=%" PRIu32 " video_timeouts=%" PRIu32
             " video_failures=%" PRIu32
             " video_accelerated=%" PRIu32
             " video_accelerator_failures=%" PRIu32
             " touch_polls=%" PRIu32
             " touch_failures=%" PRIu32 " touch_retries=%" PRIu32
             " composite_gate=%u touch_gate=%u audio_gate=%u"
             " audio_mutating_calls=%" PRIu32
             " audio_telemetry_snapshot_valid=%u audio_start_proof=%s"
             " audio_write_calls=%" PRIu32
             " audio_frames_forwarded=%" PRIu32
             " audio_nonzero_frames=%" PRIu32
             " audio_nonzero_samples=%" PRIu32
             " audio_peak=%u backend_telemetry_valid=%u"
             " backend_snapshot_sequence=%" PRIu32
             " backend_gpio30_high_attempts=%" PRIu32
             " backend_gpio30_high_successes=%" PRIu32
             " backend_gpio30_high_readbacks=%" PRIu32
             " backend_state=%u backend_running=%u"
             " pdm_created=%u pdm_enabled=%u"
             " pdm_create_successes=%" PRIu32
             " pdm_enable_successes=%" PRIu32
             " tx_created=%u tx_enabled=%u"
             " tx_create_successes=%" PRIu32
             " tx_enable_successes=%" PRIu32
             " zero_preload_frames=%" PRIu32
             " gpio30_low_attempts=%" PRIu32
             " gpio30_low_successes=%" PRIu32
             " gpio30_low_initial_readbacks=%" PRIu32
             " measured_settle_us=%" PRIu32
             " gpio30_low_second_readbacks=%" PRIu32
             " backend_write_successes=%" PRIu32
             " backend_write_failures=%" PRIu32
             " backend_frames_written=%" PRIu32
             " backend_samples_written=%" PRIu32
             " backend_nonzero_frames=%" PRIu32
             " backend_nonzero_samples=%" PRIu32
             " backend_max_abs=%" PRIu32
             " backend_rollback_attempts=%" PRIu32
             " backend_rollback_successes=%" PRIu32
             " backend_rollback_high_proofs=%" PRIu32
             " backend_resources_retained=%u backend_resources_owned=%" PRIu32
             " audio_frames=%" PRIu32 " audio_write_failures=%" PRIu32
             " audio_worker_stack_hwm=%" PRIu32
             " music_playing=%u music_paused=%u"
             " music_songs=%" PRIu32 " music_events=%" PRIu32
             " music_notes=%" PRIu32 " music_loops=%" PRIu32
             " music_frames=%" PRIu32
             " music_parse_failures=%" PRIu32
             " music_peak=%" PRIu32,
             snapshot->frame_count, video_stats.submits_started,
             video_stats.submits_completed, video_stats.submit_timeouts,
             video_stats.submit_failures,
             video_stats.accelerated_submits,
             video_stats.accelerator_failures, snapshot->touch_polls,
             snapshot->touch_failures, snapshot->touch_retries,
             (unsigned)snapshot->composite_gate,
             (unsigned)snapshot->touch_gate,
             (unsigned)snapshot->audio_gate,
             adapter_stats.invocations, have_backend_stats ? 1U : 0U,
             start_proof,
             adapter_stats.write_calls_succeeded,
             adapter_stats.frames_forwarded,
             adapter_stats.nonzero_frames_forwarded,
             adapter_stats.nonzero_samples_forwarded,
             (unsigned)adapter_stats.observed_absolute_peak,
             have_backend_stats ? 1U : 0U,
             have_backend_stats ? backend_stats.snapshot_sequence : 0U,
             have_backend_stats ? backend_stats.gpio30_high_attempts : 0U,
             have_backend_stats ? backend_stats.gpio30_high_successes : 0U,
             have_backend_stats
                 ? backend_stats.gpio30_high_readback_successes : 0U,
             have_backend_stats ? (unsigned)backend_stats.state : 2U,
             have_backend_stats && backend_stats.running ? 1U : 0U,
             have_backend_stats && backend_stats.pdm_created ? 1U : 0U,
             have_backend_stats && backend_stats.pdm_enabled ? 1U : 0U,
             have_backend_stats ? backend_stats.pdm_create_successes : 0U,
             have_backend_stats ? backend_stats.pdm_enable_successes : 0U,
             have_backend_stats && backend_stats.tx_created ? 1U : 0U,
             have_backend_stats && backend_stats.tx_enabled ? 1U : 0U,
             have_backend_stats ? backend_stats.tx_create_successes : 0U,
             have_backend_stats ? backend_stats.tx_enable_successes : 0U,
             have_backend_stats ? backend_stats.zero_preload_frames : 0U,
             have_backend_stats ? backend_stats.gpio30_low_attempts : 0U,
             have_backend_stats ? backend_stats.gpio30_low_successes : 0U,
             have_backend_stats
                 ? backend_stats.gpio30_low_initial_readback_successes : 0U,
             have_backend_stats ? backend_stats.measured_settle_us : 0U,
             have_backend_stats
                 ? backend_stats.gpio30_low_second_readback_successes : 0U,
             have_backend_stats ? backend_stats.write_successes : 0U,
             have_backend_stats ? backend_stats.write_failures : UINT32_MAX,
             have_backend_stats ? backend_stats.frames_written : 0U,
             have_backend_stats ? backend_stats.samples_written : 0U,
             have_backend_stats ? backend_stats.nonzero_frames : 0U,
             have_backend_stats ? backend_stats.nonzero_samples : 0U,
             have_backend_stats
                 ? backend_stats.maximum_absolute_magnitude : 0U,
             have_backend_stats ? backend_stats.rollback_attempts : 0U,
             have_backend_stats ? backend_stats.rollback_successes : 0U,
             have_backend_stats ? backend_stats.rollback_high_proofs : 0U,
             have_backend_stats && backend_stats.resources_retained ? 1U : 0U,
             have_backend_stats ? backend_stats.resources_owned : UINT32_MAX,
             have_audio_stats ? audio_stats.frames_rendered : 0U,
             have_audio_stats ? audio_stats.write_failures : 0U,
             have_audio_stats ? audio_stats.worker_stack_hwm_bytes : UINT32_MAX,
             have_audio_stats && audio_stats.music_playing ? 1U : 0U,
             have_audio_stats && audio_stats.music_paused ? 1U : 0U,
             have_audio_stats ? audio_stats.music_songs_started : 0U,
             have_audio_stats ? audio_stats.music_events_processed : 0U,
             have_audio_stats ? audio_stats.music_notes_started : 0U,
             have_audio_stats ? audio_stats.music_loops_completed : 0U,
             have_audio_stats ? audio_stats.music_mixed_frames : 0U,
             have_audio_stats ? audio_stats.music_parse_failures : UINT32_MAX,
             have_audio_stats
                 ? audio_stats.music_maximum_absolute_mix : 0U)) return false;
    return emit_performance_stats(snapshot, record);
}

/* Separate versioned record: the strict DIAG_V2/WORKER_V2 schemas stay
 * unchanged. These are owner-copied scalars, never live sampler/task state. */
static bool emit_touch_sampler(const doom_diag_snapshot_t *snapshot,
                               const p4_diag_record_t *record)
{
    if (!p4_diag_is_current(&s_mailbox, record)) return false;
    const doom_touch_sampler_diag_t *const touch = &snapshot->touch_sampler;
    size_t used = 0U;
    if (!append_format(&used, "\nP4_DOOM TOUCH_SAMPLER_V1"
        " touch_schema=1 diag_generation=%" PRIu32 " diag_sequence=%" PRIu64
        " diag_capture_us=%" PRIu64 " diag_emit_us=%" PRIu64
        " sampler_epoch=%" PRIu32 " sampler_present=%u sampler_active=%u"
        " stats_valid=%u age_valid=%u sampler_capture_us=%" PRIu64
        " samples=%" PRIu64 " poll_calls=%" PRIu64 " poll_total_us=%" PRIu64
        " poll_max_us=%" PRIu32 " last_success_us=%" PRId64 " age_us=%" PRIu64
        " overflows=%" PRIu32 " stale_neutralizations=%" PRIu32
        " producer_stalls=%" PRIu32 " recoveries=%" PRIu32 " fault=%" PRId32 "\n",
        record->generation, record->sequence, record->captured_us,
        (uint64_t)esp_timer_get_time(), touch->epoch,
        touch->present ? 1U : 0U, touch->active ? 1U : 0U,
        touch->stats_valid ? 1U : 0U, touch->age_valid ? 1U : 0U,
        touch->captured_us, touch->samples, touch->poll_calls, touch->poll_total_us,
        touch->poll_max_us, touch->last_completed_us, touch->age_us,
        touch->overflows, touch->stale_neutralizations, touch->producer_stalls,
        touch->recoveries, touch->fault) ||
        !p4_diag_is_current(&s_mailbox, record)) return false;
    return admit_line(used);
}

/* Separate schema and copied observations; no consumer heap/storage access.
 * Static phase observations retain their event and sample times on repetition. */
static bool emit_memory(const doom_diag_snapshot_t *snapshot,
                         const p4_diag_record_t *record)
{
    static const char *const names[DOOM_MEMORY_PHASE_COUNT] = {
        "engine-start", "pre-zone", "post-zone", "engine-ready", "first-map",
        "checkpoint-capture", "checkpoint-restore", "runtime"
    };
    _Static_assert(DOOM_MEMORY_PHASE_COUNT == 8, "MEMORY_V1 phase mapping is fixed");
    const doom_memory_diag_t *const memory = &snapshot->memory;
    unsigned parts = 0U;
    for (unsigned phase = 0U; phase < DOOM_MEMORY_PHASE_COUNT; ++phase)
        if (memory->present && (memory->valid_mask & (UINT32_C(1) << phase))) ++parts;
    unsigned part = 0U;
    for (unsigned phase = 0U; phase <= DOOM_MEMORY_PHASE_COUNT; ++phase) {
        const bool valid = phase < DOOM_MEMORY_PHASE_COUNT && memory->present &&
            (memory->valid_mask & (UINT32_C(1) << phase));
        if (!valid && (phase < DOOM_MEMORY_PHASE_COUNT || parts)) continue;
        if (!p4_diag_is_current(&s_mailbox, record)) return false;
        const doom_memory_observation_t observation = valid
            ? memory->phases[phase] : (doom_memory_observation_t){0};
        size_t used = 0U;
        if (!append_format(&used, "\nP4_DOOM MEMORY_V1"
            " memory_schema=1 diag_generation=%" PRIu32 " diag_sequence=%" PRIu64
            " diag_capture_us=%" PRIu64 " diag_emit_us=%" PRIu64
            " memory_part=%u memory_parts=%u phase_index=%u phase=%s"
            " memory_present=%u observation_valid=%u event_us=%" PRIu64
            " observed_us=%" PRIu64 " free=%" PRIu64 " largest=%" PRIu64
            " boot_min_free=%" PRIu64 " sampled_since_us=%" PRIu64
            " sampled_min_free=%" PRIu64 " sample_count=%" PRIu32
            " zone_requested=%" PRIu64 " arena_resident=%" PRIu64
            " zone_admission_error=%" PRId32 " zone_attempts=%" PRIu32
            " zone_allocated=%u\n",
            record->generation, record->sequence, record->captured_us,
            (uint64_t)esp_timer_get_time(), ++part, parts ? parts : 1U,
            phase, valid ? names[phase] : "none", memory->present ? 1U : 0U,
            valid ? 1U : 0U, observation.event_us, observation.observed_us,
            observation.free_bytes, observation.largest_bytes,
            observation.boot_min_free, memory->sampled_since_us,
            memory->sampled_min_free, memory->sample_count,
            memory->zone_requested_bytes, memory->arena_resident_bytes,
            memory->zone_admission_error, memory->zone_allocation_attempts,
            memory->zone_allocation_succeeded ? 1U : 0U) ||
            !p4_diag_is_current(&s_mailbox, record) || !admit_line(used)) return false;
    }
    return true;
}

static bool drain_one(void)
{
    if (!p4_diag_try_take(&s_mailbox, &s_record)) return false;
    if (s_record.payload_bytes == sizeof(s_snapshot) &&
        p4_diag_is_current(&s_mailbox, &s_record)) {
        memcpy(&s_snapshot, s_record.payload, sizeof(s_snapshot));
        if (s_snapshot.captured_us == s_record.captured_us &&
            emit_snapshot(&s_snapshot, &s_record)) {
            (void)emit_touch_sampler(&s_snapshot, &s_record);
            (void)emit_memory(&s_snapshot, &s_record);
            emit_transport_status();
        }
    }
    return true;
}

/* Independent SPSC producer: video callback. Copy before admission; never
 * hold a display lease, borrowed state or global lock. */
static bool drain_video(void)
{
    doom_video_metrics_t m;
    if (!doom_video_metrics_take(&m)) return false;
    size_t used = 0U;
    if (append_format(&used, "\nP4_DOOM_VIDEO WORKER_V2"
        " video_schema=2 video_generation=%" PRIu32 " video_sequence=%" PRIu64
        " capture_us=%" PRIu64 " emit_us=%" PRIu64 " closed=%u"
        " completed=%" PRIu32 " failures=%" PRIu32 " sample_frames=%" PRIu64
        " convert_total_us=%" PRIu64 " display_total_us=%" PRIu64
        " service_max_us=%" PRIu64 " stack_free_bytes=%" PRIu32 " error=%d"
        " indexed_enabled=%u indexed_completed=%" PRIu32 " xrgb_completed=%" PRIu32
        " window_frames=%" PRIu32 " window_convert_avg_us=%" PRIu32
        " window_display_avg_us=%" PRIu32 " window_service_max_us=%" PRIu32
        " lifetime_failures=%" PRIu64 " last_fault_generation=%" PRIu32
        " last_fault_capture_us=%" PRIu64 " last_fault_error=%d\n",
        m.generation, m.sequence, m.captured_us, (uint64_t)esp_timer_get_time(),
        m.closed ? 1U : 0U, m.completed, m.failures, m.sample_frames,
        m.convert_total_us, m.display_total_us, m.service_max_us,
        m.stack_free_bytes, m.error, m.indexed_enabled ? 1U : 0U,
        m.indexed_completed, m.xrgb_completed, m.window_frames,
        m.window_convert_avg_us, m.window_display_avg_us, m.window_service_max_us,
        m.lifetime_failures,
        m.last_fault_generation, m.last_fault_capture_us, m.last_fault_error) &&
        admit_line(used)) emit_transport_status();
    return true;
}

static void diagnostic_task(void *unused)
{
    (void)unused;
    for (;;) {
        (void)drain_video();
        (void)drain_one();
        TickType_t idle_ticks = pdMS_TO_TICKS(10U);
        if (idle_ticks == 0U) idle_ticks = 1U;
        vTaskDelay(idle_ticks);
    }
}

void doom_diagnostics_begin(void)
{
    if (!s_initialized) {
        p4_diag_init(&s_mailbox);
        s_initialized = true;
    }
    if (!s_task_attempted) {
        s_task_attempted = true;
        s_task_ready = xTaskCreate(diagnostic_task, "doom_diag", DIAG_STACK_BYTES,
            NULL, tskIDLE_PRIORITY, NULL) == pdPASS;
    }
    if (s_task_ready) (void)p4_diag_begin_generation(&s_mailbox);
    /* Failed task creation disables periodic diagnostics. Never emit inline. */
}

void doom_diagnostics_end(void)
{
    if (s_initialized) p4_diag_end_generation(&s_mailbox);
}

bool doom_diagnostics_reserve(p4_diag_reservation_t *reservation)
{
    return s_task_ready && p4_diag_try_reserve(&s_mailbox, reservation);
}

void doom_diagnostics_publish(p4_diag_reservation_t reservation,
                              const doom_diag_snapshot_t *snapshot)
{
    if (!snapshot) {
        p4_diag_cancel(&s_mailbox, reservation);
        return;
    }
    (void)p4_diag_publish(&s_mailbox, reservation, snapshot->captured_us,
                         snapshot, sizeof(*snapshot));
}
