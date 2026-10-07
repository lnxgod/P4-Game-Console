#!/usr/bin/env python3
"""Compile the selected Doom consumer against real input and queue logic."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "apps/doom_embedded_touch_audio/main/doom_embedded_touch_audio_main.c"

def function(source, name, result='void'):
    start = source.index(f"static {result} {name}(")
    body = source.index("{", start)
    depth, end = 1, body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "platform/touch_sampler.h"
#include "platform_touch_sample_queue.h"
#include "touch_controls.h"
#include "degraded_policy.h"
#define P4_DOOM_USB_DEBUG 1
#define TOUCH_POLL_INTERVAL_MS 16U
#define TOUCH_RETRY_INTERVAL_MS 5000U
struct platform_touch_sampler { platform_touch_sample_queue_t queue; };
static struct platform_touch_sampler sampler;
static platform_touch_sampler_t *s_touch_sampler;
static doom_touch_input_t s_touch_input;
static int *s_shared_bus;
static platform_touch_t *s_touch;
static bool s_touch_ready, s_touch_cleanup_proven;
static uint32_t s_last_touch_poll_ms, s_last_touch_retry_ms;
static uint32_t s_touch_polls, s_touch_poll_failures;
static int64_t now_us;
static unsigned physical_reads, released_device, stop_calls, debug_reads;
static esp_err_t stop_result, destroy_result;
static bool debug_pending;
static platform_touch_frame_t debug_frame_value;

static uint32_t ticks_ms(void) { return (uint32_t)(now_us / 1000); }
static bool console_os_debug_touch(platform_touch_frame_t *frame)
{
    ++debug_reads;
    if (!debug_pending) return false;
    *frame = debug_frame_value;
    debug_pending = false;
    return true;
}
static void touch_sampler_diag_capture(void) {}
static void neutralize_touch_input(void)
{ (void)doom_touch_audio_force_neutral(&s_touch_input); }
static bool try_touch_create(bool force) { (void)force; return false; }
esp_err_t platform_touch_poll(platform_touch_t *touch, platform_touch_frame_t *frame)
{ (void)touch; ++physical_reads; platform_touch_frame_neutral(frame); return ESP_OK; }
static void log_touch_degraded(const char *stage, esp_err_t error, bool force)
{ (void)stage; (void)error; (void)force; }
esp_err_t platform_touch_destroy(platform_touch_t **touch)
{ ++released_device; if (destroy_result == ESP_OK) *touch = NULL; return destroy_result; }
esp_err_t platform_touch_sampler_read(platform_touch_sampler_t *s,
    platform_touch_frame_t *frame, bool *available)
{ return platform_touch_sample_queue_take(&s->queue, now_us, frame, available); }
void platform_touch_sampler_discard(platform_touch_sampler_t *s)
{ if (s != NULL) platform_touch_sample_queue_discard(&s->queue); }
esp_err_t platform_touch_sampler_stop(platform_touch_sampler_t **s, uint32_t wait)
{ assert(wait == 0U || wait == 100U); ++stop_calls;
  if (stop_result == ESP_OK) *s = NULL; return stop_result; }

/* PRODUCTION_FUNCTION */

static platform_touch_frame_t contact(bool down, int64_t acquired)
{
    platform_touch_frame_t frame;
    platform_touch_frame_neutral(&frame);
    if (down) {
        frame.contact_count = 1U;
        frame.timestamp_us = acquired;
        frame.contacts[0].x = 1104U;
        frame.contacts[0].y = 544U;
    }
    return frame;
}
static void publish(bool down, int64_t acquired, int64_t completed)
{
    platform_touch_frame_t frame = contact(down, acquired);
    assert(platform_touch_sample_queue_publish(&sampler.queue, &frame,
        ESP_OK, completed) == ESP_OK);
}
static unsigned drain(bool pressed)
{
    doom_touch_event_t event;
    unsigned count = 0U;
    while (doom_touch_input_next(&s_touch_input, &event)) {
        assert(event.pressed == (pressed ? 1U : 0U));
        ++count;
    }
    return count;
}
static void reset(void)
{
    now_us = 1000;
    platform_touch_sample_queue_init(&sampler.queue, now_us);
    doom_touch_input_init(&s_touch_input);
    s_touch_sampler = &sampler;
    s_touch_ready = true;
    s_touch_cleanup_proven = true;
    s_last_touch_poll_ms = 0U;
    s_last_touch_retry_ms = 0U;
    s_touch_polls = s_touch_poll_failures = 0U;
    physical_reads = released_device = stop_calls = debug_reads = 0U;
    debug_pending = false;
    stop_result = ESP_ERR_TIMEOUT;
    destroy_result = ESP_OK;
    (void)s_touch; (void)s_shared_bus; (void)s_last_touch_poll_ms;
    (void)release_retained_touch; (void)try_touch_create;
}
int main(void)
{
    reset();
    publish(true, 1000, 1000);
    publish(false, 0, 18000);
    now_us = 20000;
    service_touch();
    assert(physical_reads == 0U);
    assert(drain(true) == 2U); /* Fire and menu-accept share this control. */
    service_touch();
    assert(drain(false) == 2U); /* A sub-frame tap keeps its release. */

    reset();
    publish(true, 1000, 1000);
    now_us = 1000;
    service_touch();
    assert(drain(true) == 2U);
    debug_pending = true;
    debug_frame_value = contact(false, 0);
    now_us = 2000;
    service_touch(); /* Immediate one-shot debug release, no 16 ms gate. */
    assert(!debug_pending && drain(false) == 2U);
    service_touch();
    assert(drain(false) == 0U); /* No pre-override physical frame replay. */
    assert(physical_reads == 0U && released_device == 0U);

    reset();
    publish(true, 1000, 1000);
    now_us = 1000;
    service_touch();
    assert(drain(true) == 2U);
    now_us = 52000;
    service_touch(); /* A scheduling stall releases keys, retaining the worker. */
    assert(drain(false) == 2U && s_touch_ready && s_touch_cleanup_proven);
    assert(stop_calls == 0U && released_device == 0U && physical_reads == 0U);
    service_touch();
    assert(drain(false) == 0U && stop_calls == 0U);
    publish(true, 53000, 54000);
    now_us = 55000;
    service_touch(); /* A new physical sample restores input automatically. */
    assert(drain(true) == 2U && s_touch_ready && s_touch_sampler == &sampler);
    assert(stop_calls == 0U && physical_reads == 0U);

    reset();
    publish(true, 1000, 1000);
    now_us = 1000;
    service_touch();
    assert(drain(true) == 2U);
    publish(true, 60000, 60000); /* Producer resumes before foreground observes gap. */
    now_us = 61000;
    service_touch();
    assert(drain(false) == 2U && s_touch_ready);
    service_touch();
    assert(drain(true) == 2U && stop_calls == 0U && physical_reads == 0U);

    assert(platform_touch_sample_queue_publish(&sampler.queue, NULL,
        ESP_ERR_TIMEOUT, 62000) == ESP_ERR_TIMEOUT);
    now_us = 63000;
    service_touch(); /* A genuine hardware error retains the terminal behavior. */
    assert(drain(false) == 2U && !s_touch_ready && !s_touch_cleanup_proven);
    assert(stop_calls == 1U && released_device == 0U && physical_reads == 0U);

    reset();
    publish(true, 1000, 1000);
    now_us = 1000;
    debug_pending = true;
    debug_frame_value = contact(true, 1000);
    debug_frame_value.contact_count = 99U;
    service_touch();
    assert(s_touch_ready && stop_calls == 0U && released_device == 0U);
    assert(drain(false) == 0U);

    reset();
    s_touch_ready = false;
    debug_pending = true;
    debug_frame_value = contact(true, 1000);
    service_touch();
    assert(drain(true) == 2U && physical_reads == 0U);

    reset();
    int touch_token;
    s_touch = (platform_touch_t *)&touch_token;
    assert(!release_retained_touch()); /* Worker still owns the hardware. */
    assert(released_device == 0U && s_touch && s_touch_sampler);
    assert(!s_touch_cleanup_proven);
    stop_result = ESP_OK;
    destroy_result = ESP_FAIL;
    assert(!release_retained_touch()); /* Joined worker, failed driver cleanup. */
    assert(released_device == 1U && s_touch && !s_touch_sampler);
    assert(!s_touch_cleanup_proven);
    destroy_result = ESP_OK;
    assert(release_retained_touch());
    assert(released_device == 2U && !s_touch && !s_touch_sampler);
    assert(s_touch_cleanup_proven);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="p4-touch-consumer-") as directory:
    folder = Path(directory)
    test = folder / "test.c"
    source = SOURCE.read_text()
    production = function(source, 'release_retained_touch', 'bool') + '\n' + function(source, 'service_touch')
    test.write_text(HARNESS.replace("/* PRODUCTION_FUNCTION */", production))
    binary = folder / "test"
    includes = [
        ROOT / "components/platform_touch/include",
        ROOT / "components/platform_touch/src",
        ROOT / "components/platform_touch/include",
        ROOT / "components/platform_touch/host/include",
        ROOT / "components/platform_board/include",
        ROOT / "components/doom_touch_input/include",
        ROOT / "apps/doom_embedded_touch_audio/main",
        ROOT / "third_party/doomgeneric/doomgeneric",
    ]
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
        "-Wconversion", "-Wshadow", "-Werror", "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer", "-DCONFIG_P4_BOARD_M5STACK_TAB5=1",
        "-DDOOM_TOUCH_SCREEN_WIDTH=1280U", "-DDOOM_TOUCH_SCREEN_HEIGHT=720U",
        str(test), str(ROOT / "components/platform_touch/src/platform_touch_sample_queue.c"),
        str(ROOT / "components/platform_touch/src/platform_touch_frame.c"),
        str(ROOT / "components/doom_touch_input/src/input.c"),
        str(ROOT / "components/doom_touch_input/src/overlay.c"),
        str(ROOT / "apps/doom_embedded_touch_audio/main/touch_controls.c"),
        str(ROOT / "apps/doom_embedded_touch_audio/main/degraded_policy.c"),
        *[arg for p in includes for arg in ("-I", str(p))], "-o", str(binary)
    ], check=True)
    subprocess.run([str(binary)], check=True)
print("Doom sampled input: short tap, debug release, stall recovery, fault retention, remote fallback passed")
