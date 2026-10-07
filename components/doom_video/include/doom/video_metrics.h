// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef DOOM_VIDEO_METRICS_H
#define DOOM_VIDEO_METRICS_H
#include <stdbool.h>
#include <stdint.h>
/* Tab5-only copied diagnostics. Values are cumulative within video generation;
 * sequence is boot lifetime. Endpoint gaps mean unobserved reports; they do
 * not alone prove handoff coalescence.
 * Capture clock is esp_timer microseconds, never the delayed ESP log prefix.
 * An ended generation may be emitted late, with its original identity intact.
 * Historical per-window maxima cannot be reconstructed from cumulative maxima.
 */
typedef struct {
    uint64_t captured_us, sequence, sample_frames;
    uint64_t convert_total_us, display_total_us, service_max_us;
    uint64_t lifetime_failures, last_fault_capture_us;
    uint32_t generation, completed, failures;
    uint32_t indexed_completed, xrgb_completed, stack_free_bytes;
    uint32_t window_frames, window_convert_avg_us, window_display_avg_us;
    uint32_t window_service_max_us;
    uint32_t last_fault_generation;
    int error, last_fault_error;
    bool closed, indexed_enabled;
} doom_video_metrics_t;
/* Foreground before worker start; never reset the live handoff. Producer
 * ownership passes to worker at create, and back to foreground only at join. */
uint32_t doom_video_metrics_begin(void);
/* Sole producer, bounded copy + lock-free 32-bit exchange, no allocation/wait.
 * First failure is latched by display_worker; lifetime count/last failure are
 * retained across game visits even if an entire visit's reports coalesce. */
void doom_video_metrics_publish(const doom_video_metrics_t *snapshot);
/* Sole persistent diagnostic consumer. No borrowed pointers or held slots
 * escape this call. A blocked output sink cannot block producer publication. */
bool doom_video_metrics_take(doom_video_metrics_t *out);
#endif
