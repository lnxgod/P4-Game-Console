#!/usr/bin/env python3
"""Exercise the production Doom debug-input adapters with host input mocks."""

from pathlib import Path
import os
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "apps/doom_embedded_touch_audio/main/doom_embedded_touch_audio_main.c"


def function(source: str, name: str) -> str:
    start = source.index(f"static void {name}(")
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "gamepad/gamepad.h"
#include "doom_gamepad/input.h"
#include "p4/game.h"

#define P4_DOOM_USB_DEBUG 1
#define TOUCH_POLL_INTERVAL_MS 16U
#define TOUCH_RETRY_INTERVAL_MS 5000U
#define ESP_OK 0
#define ESP_ERR_INVALID_RESPONSE 1
typedef int esp_err_t;
typedef struct { unsigned contacts; } platform_touch_frame_t;
typedef platform_touch_frame_t doom_touch_frame_t;
static int s_touch_input;
static int *s_shared_bus;
static int *s_touch;
static bool s_touch_ready;
static bool s_touch_cleanup_proven;
static uint32_t s_last_touch_poll_ms;
static uint32_t s_last_touch_retry_ms;
static uint32_t s_touch_polls;
static uint32_t s_touch_poll_failures;
static uint32_t now;
static bool debug_pending;
static unsigned debug_contacts;
static unsigned debug_reads;
static unsigned physical_reads;
static unsigned released_device;
static unsigned applied_contacts;
static int physical_result;

static bool doom_touch_input_idle(const int *input) { (void)input; return true; }
static uint32_t ticks_ms(void) { return now; }
static bool console_os_debug_touch(platform_touch_frame_t *frame)
{
    ++debug_reads;
    if (!debug_pending) return false;
    frame->contacts = debug_contacts;
    debug_pending = false;
    return true;
}
static void neutralize_touch_input(void) { applied_contacts = 0U; }
static bool doom_touch_audio_retry_due(uint32_t a, uint32_t b, uint32_t c,
                                      bool d, bool e)
{ (void)a; (void)b; (void)c; (void)d; (void)e; return false; }
static bool try_touch_create(bool force) { (void)force; return false; }
static int platform_touch_poll(int *touch, platform_touch_frame_t *frame)
{ (void)touch; ++physical_reads; frame->contacts = 2U; return physical_result; }
static bool doom_touch_audio_frame_from_platform(
    const platform_touch_frame_t *source, doom_touch_frame_t *target)
{
    if (source == NULL || source->contacts > 5U) return false;
    *target = *source;
    return true;
}
static void log_touch_degraded(const char *stage, int error, bool force)
{ (void)stage; (void)error; (void)force; }
static bool release_retained_touch(void) { ++released_device; return true; }
static bool doom_touch_input_update(int *input, const doom_touch_frame_t *frame)
{ (void)input; applied_contacts = frame->contacts; return true; }

/* PRODUCTION_FUNCTIONS */

static void drain(doom_gamepad_input_t *input)
{
    doom_gamepad_event_t event;
    while (doom_gamepad_input_next(input, &event)) {}
}

int main(void)
{
    gamepad_state_t physical;
    gamepad_state_init(&physical);
    gamepad_state_t combined = physical;
    merge_debug_gamepad(&combined, P4_BUTTON_UP | P4_BUTTON_A | P4_BUTTON_B);
    assert(combined.connected == 1U);
    assert(combined.dpad == GAMEPAD_DPAD_UP);
    assert(combined.buttons == (GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH) |
                                GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_EAST)));
    assert(physical.connected == 0U && physical.buttons == 0U);

    doom_gamepad_input_t input;
    doom_gamepad_input_init(&input);
    assert(doom_gamepad_input_update(&input, &combined) == GAMEPAD_OK);
    drain(&input);
    combined = physical;
    merge_debug_gamepad(&combined, 0U); /* Expired debug lease, no real pad. */
    assert(doom_gamepad_input_update(&input, &combined) == GAMEPAD_OK);
    doom_gamepad_event_t event;
    unsigned releases = 0U;
    while (doom_gamepad_input_next(&input, &event)) {
        assert(event.pressed == 0U);
        ++releases;
    }
    assert(releases == 4U); /* Up, fire, use and Enter. */

    assert(gamepad_state_connect(&physical, 100U) == GAMEPAD_OK);
    physical.buttons = GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH);
    physical.left_x = 16000;
    const gamepad_state_t untouched = physical;
    combined = physical;
    merge_debug_gamepad(&combined, P4_BUTTON_UP | P4_BUTTON_A);
    assert(memcmp(&physical, &untouched, sizeof(physical)) == 0);
    assert(combined.left_x == 16000);
    assert(doom_gamepad_input_update(&input, &combined) == GAMEPAD_OK);
    drain(&input);
    combined = physical;
    merge_debug_gamepad(&combined, 0U);
    assert(doom_gamepad_input_update(&input, &combined) == GAMEPAD_OK);
    assert(doom_gamepad_input_next(&input, &event));
    assert(event.action == DOOM_GAMEPAD_ACTION_UP && event.pressed == 0U);
    assert(!doom_gamepad_input_next(&input, &event)); /* Real fire stays held. */

    combined = physical;
    merge_debug_gamepad(&combined, UINT32_C(0xffffff00));
    assert(memcmp(&physical, &combined, sizeof(physical)) == 0);
    combined.connected = 0U; /* Defensive: stale disconnected data is ignored. */
    combined.right_trigger = UINT16_MAX;
    merge_debug_gamepad(&combined, P4_BUTTON_DOWN | P4_BUTTON_LEFT |
                                  P4_BUTTON_RIGHT | P4_BUTTON_START | P4_BUTTON_BACK);
    assert(combined.left_x == 0 && combined.right_trigger == 0U);
    assert(combined.dpad == (GAMEPAD_DPAD_DOWN | GAMEPAD_DPAD_LEFT |
                             GAMEPAD_DPAD_RIGHT));
    assert(combined.buttons == (GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_START) |
                                GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_BACK)));

    now = 16U;
    debug_pending = true;
    debug_contacts = 1U;
    service_touch(); /* Debug touch works when physical hardware is unavailable. */
    assert(applied_contacts == 1U && physical_reads == 0U);
    assert(debug_reads == 1U && s_touch_polls == 1U);
    now = 20U;
    debug_pending = true;
    debug_contacts = 0U;
    service_touch(); /* Throttling must not consume the one-shot release. */
    assert(debug_pending && debug_reads == 1U && applied_contacts == 1U);
    now = 32U;
    service_touch();
    assert(!debug_pending && applied_contacts == 0U && debug_reads == 2U);
    assert(released_device == 0U);

    s_touch_ready = true;
    now = 48U;
    service_touch(); /* The next frame returns to the real touchscreen. */
    assert(physical_reads == 1U && applied_contacts == 2U);
    now = 64U;
    physical_result = ESP_ERR_INVALID_RESPONSE;
    service_touch();
    assert(applied_contacts == 0U && !s_touch_ready && released_device == 1U);
    assert(s_touch_poll_failures == 1U);
    s_touch_ready = true;
    now = 80U;
    debug_pending = true;
    debug_contacts = 99U;
    service_touch(); /* A malformed injected frame cannot tear down hardware. */
    assert(applied_contacts == 0U && s_touch_ready && released_device == 1U);
    return 0;
}
'''


def main() -> None:
    source = SOURCE.read_text()
    production = "\n\n".join(function(source, name) for name in (
        "merge_debug_gamepad", "service_touch"))
    with tempfile.TemporaryDirectory(prefix="p4-doom-debug-") as directory:
        folder = Path(directory)
        test = folder / "test.c"
        test.write_text(HARNESS.replace("/* PRODUCTION_FUNCTIONS */", production))
        binary = folder / "test"
        subprocess.run([
            os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
            "-Wconversion", "-Wshadow", "-Werror", "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer", str(test),
            str(ROOT / "components/gamepad_core/src/gamepad.c"),
            str(ROOT / "components/doom_gamepad_input/src/input.c"),
            "-I", str(ROOT / "components/gamepad_core/include"),
            "-I", str(ROOT / "components/doom_gamepad_input/include"),
            "-I", str(ROOT / "components/p4_game_api/include"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("Doom debug input: physical merge, expiry release, touch handoff passed")


if __name__ == "__main__":
    main()
