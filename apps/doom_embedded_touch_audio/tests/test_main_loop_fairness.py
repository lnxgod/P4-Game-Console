#!/usr/bin/env python3
"""Compile the real outer Doom loop; no device, SDK or scheduler emulation."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

SOURCE = (Path(sys.argv[1]) if len(sys.argv) > 1 else
          Path(__file__).resolve().parents[1] / 'main/doom_embedded_touch_audio_main.c')

def outer_loop(text):
    marker = '    for (;;) {\n        doomgeneric_Tick();'
    assert text.count(marker) == 1, 'outer loop must remain unambiguous'
    start = text.index(marker)
    body = text.index('{', start)
    depth, end = 1, body + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define P4_DOOM_INDEXED_DRAM_ENABLED 1
#define ESP_LOGI(...) ((void)0)
static jmp_buf done;
static unsigned ticks, blocks, diagnostic_ends, closes, cleanups, rendered;
static unsigned net_polls, input_polls, memory_services, mode;
static int s_frame_error;
static bool s_indexed_engine_live;
static char order[8192];
static unsigned order_n;
static void event(char e) { assert(order_n < sizeof order); order[order_n++] = e; }
static void doomgeneric_Tick(void) {
    if (ticks == 1000U) longjmp(done, 1);
    ++ticks; ++net_polls; ++input_polls; event('T');
    /* Frames may cost more than one 35 Hz engine tic; replay/no-frame can
     * return immediately. Neither case promises an incidental blocking call. */
    if (mode == 0U) ++rendered;
    if (mode == 4U && ticks == 4U) s_frame_error = 7;
}
static void doom_memory_service_events(void) { ++memory_services; }
static bool doomgeneric_QuitRequested(void) {
    return (mode == 3U || mode == 5U) && ticks == 4U;
}
static void doom_diagnostics_end(void) { ++diagnostic_ends; event('D'); }
#ifdef P4_CONSOLE_OS_EMBEDDED
static bool release_audio(void) { event('A'); return mode != 5U; }
static void close_engine_wads(void) { ++closes; event('W'); }
static void composite_cleanup(void) { ++cleanups; event('C'); }
#endif
static _Noreturn void esp_restart(void) { event('R'); longjmp(done, 2); }
static _Noreturn void halt_dark(const char *stage, int error) {
    assert(strcmp(stage, "engine-frame") == 0 && error == 7);
    event('H'); longjmp(done, 3);
}
#if defined(P4_CONSOLE_OS_EMBEDDED) && CONFIG_P4_BOARD_M5STACK_TAB5
void vTaskDelay(unsigned tick_count) {
    /* A zero-tick yield would not make a priority-0 idle task eligible. */
    assert(tick_count == 1U); ++blocks; event('B');
}
#endif
static void actual_outer_loop(void) {
/* ACTUAL_OUTER_LOOP */
}
int main(void) {
    for (mode = 0U; mode < 6U; ++mode) {
        ticks = blocks = diagnostic_ends = closes = cleanups = rendered = 0U;
        net_polls = input_polls = memory_services = order_n = 0U;
        s_frame_error = ESP_OK; s_indexed_engine_live = true;
        int result = setjmp(done);
        if (result == 0) actual_outer_loop();
        const bool terminal = mode >= 3U;
        const unsigned completed = terminal ? 3U : 1000U;
        assert(ticks == (terminal ? 4U : 1000U));
        assert(net_polls == ticks && input_polls == ticks);
        assert(memory_services == ticks);
#if defined(P4_CONSOLE_OS_EMBEDDED) && CONFIG_P4_BOARD_M5STACK_TAB5
        assert(blocks == completed);
        for (unsigned i = 0U; i < completed; ++i) {
            assert(order[i * 2U] == 'T' && order[i * 2U + 1U] == 'B');
        }
#else
        (void)completed;
        assert(blocks == 0U);
#endif
        if (!terminal) {
            assert(result == 1 && diagnostic_ends == 0U && cleanups == 0U);
            assert(rendered == (mode == 0U ? 1000U : 0U));
        } else if (mode == 4U) {
            assert(result == 3 && diagnostic_ends == 0U && cleanups == 0U);
            assert(order[order_n - 1U] == 'H');
        } else {
            assert(result == 2 && diagnostic_ends == 1U && !s_indexed_engine_live);
#ifdef P4_CONSOLE_OS_EMBEDDED
            assert(cleanups == 1U && closes == (mode == 5U ? 0U : 1U));
#else
            assert(cleanups == 0U && closes == 0U);
#endif
            assert(order[order_n - 1U] == 'R');
        }
    }
    puts("6 real-loop cases PASS");
}
'''

def main():
    source = SOURCE.read_text()
    rendered = HARNESS.replace('/* ACTUAL_OUTER_LOOP */', outer_loop(source))
    with tempfile.TemporaryDirectory(prefix='doom-fairness-') as tmp:
        base = Path(tmp)
        cfile = base / 'test.c'; cfile.write_text(rendered)
        for name, flags in [
            ('embedded-tab5', ['-DP4_CONSOLE_OS_EMBEDDED=1', '-DCONFIG_P4_BOARD_M5STACK_TAB5=1']),
            ('embedded-legacy', ['-DP4_CONSOLE_OS_EMBEDDED=1', '-DCONFIG_P4_BOARD_M5STACK_TAB5=0']),
            ('standalone-tab5', ['-DCONFIG_P4_BOARD_M5STACK_TAB5=1']),
            ('standalone-legacy', ['-DCONFIG_P4_BOARD_M5STACK_TAB5=0']),
        ]:
            binary = base / name
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-Wpedantic', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                *flags, str(cfile), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
            print(name + ': PASS', flush=True)
    print('24 extracted-loop configuration/lifecycle cases PASS')

if __name__ == '__main__': main()
