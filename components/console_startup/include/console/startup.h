// SPDX-License-Identifier: MIT
#ifndef CONSOLE_STARTUP_H
#define CONSOLE_STARTUP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include "console/brand.h"
#define CONSOLE_STARTUP_COMPLETE UINT_MAX
enum { CONSOLE_STARTUP_SAMPLE_RATE = 16000, CONSOLE_STARTUP_LOGO_BYTES = 384 * 384 * 3 };
/** Render at 768x480, 1152x720 or 1280x720. The logo is the reviewed RGB565 LE + alpha8 mark.
 * repaint=false updates only the bottom status/animation band of an existing frame.
 * animation is a monotonic frame index; CONSOLE_STARTUP_COMPLETE fills the track. */
bool console_startup_render(uint16_t *pixels, size_t stride, size_t width,
    size_t height, const uint8_t *logo, size_t logo_bytes,
    unsigned animation, const char *status, bool repaint);
/** Native perspective fly-in. elapsed_ms is wall time, not a required duration.
 * repaint=false preserves the static title; complete immediately settles the logo.
 * No hardware ownership, delays or allocation. All status text stays stationary. */
bool console_startup_render_flight(uint16_t *pixels, size_t stride, size_t width,
    size_t height, const uint8_t *logo, size_t logo_bytes, uint32_t elapsed_ms,
    const char *status, bool complete, bool repaint);
/** Original one-second five-chord fanfare stereo PCM, bounded to +/-12000. No hardware ownership.
 * The board's existing output service applies the saved boot volume exactly once. */
size_t console_startup_audio_frames(void);
const char *console_startup_audio_status(size_t frame);
bool console_startup_audio_render(int16_t *stereo, size_t first_frame, size_t frames);
#endif
