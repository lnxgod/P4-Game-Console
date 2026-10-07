#!/usr/bin/env python3
"""Compile owner snapshot helpers and verify lifecycle retention against their real contract."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'apps/doom_embedded_touch_audio/main/doom_embedded_touch_audio_main.c'
HEADER = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / 'apps/doom_embedded_touch_audio/main/doom_diagnostics.h'

def function(source, name, result='void'):
    start = source.index(f'static {result} {name}(')
    body = source.index('{', start)
    depth, end = 1, body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

source = SOURCE.read_text()
production = '\n'.join((function(source, 'touch_sampler_diag_capture'),
    function(source, 'touch_sampler_diag_start_result'),
    function(source, 'touch_sampler_diag_snapshot', 'doom_touch_sampler_diag_t')))
header = HEADER.read_text()
end = header.index('} doom_touch_sampler_diag_t;') + len('} doom_touch_sampler_diag_t;')
start = header.rindex('typedef struct {', 0, end)
contract = header[start:end]
for name, result in (('release_retained_touch','bool'),('service_touch','void')):
    f = function(source, name, result)
    assert f.index('touch_sampler_diag_capture();') < f.index('platform_touch_sampler_stop(')
f = function(source, 'try_touch_create', 'bool')
assert f.index('platform_touch_sampler_start(') < f.index('touch_sampler_diag_start_result(result);')
f = function(source, 'log_runtime_stats')
assert f.index('doom_diagnostics_reserve(') < f.index('snapshot.touch_sampler = touch_sampler_diag_snapshot();') < f.index('doom_diagnostics_publish(')

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "platform/touch_sampler.h"
/* DIAG_CONTRACT */
struct platform_touch_sampler { int token; };
static struct platform_touch_sampler handle;
static platform_touch_sampler_t *s_touch_sampler;
static doom_touch_sampler_diag_t s_touch_sampler_diag;
static uint32_t s_touch_sampler_epoch;
static platform_touch_sampler_stats_t provided;
static int64_t now_us;
static esp_err_t get_result;
static unsigned getter_calls;
static int64_t esp_timer_get_time(void) { return now_us; }
esp_err_t platform_touch_sampler_snapshot(platform_touch_sampler_t *sampler,
    platform_touch_sampler_stats_t *out)
{
    assert(sampler == &handle); ++getter_calls;
    if (get_result != ESP_OK) return get_result;
    *out = provided;
    ++now_us; /* Observation must be captured after the copied getter. */
    return ESP_OK;
}
/* PRODUCTION_HELPERS */
static void reset(void)
{
    s_touch_sampler = NULL;
    memset(&s_touch_sampler_diag, 0, sizeof(s_touch_sampler_diag));
    memset(&provided, 0, sizeof(provided));
    s_touch_sampler_epoch = 0U;
    getter_calls = 0U;
    now_us = 1000;
    get_result = ESP_OK;
}
int main(void)
{
    reset();
    doom_touch_sampler_diag_t out = touch_sampler_diag_snapshot();
    assert(!out.present && !out.active && !out.stats_valid && !out.age_valid);
    assert(getter_calls == 0U);

    s_touch_sampler = &handle;
    provided.samples = UINT64_MAX;
    provided.poll_calls = UINT64_MAX - 1U;
    provided.poll_total_us = UINT64_MAX - 2U;
    provided.poll_max_us = UINT32_MAX;
    provided.overflows = 3U;
    provided.stale_neutralizations = 4U;
    provided.producer_stalls = 5U;
    provided.recoveries = 6U;
    provided.last_completed_us = 995;
    provided.fault = ESP_ERR_TIMEOUT;
    touch_sampler_diag_start_result(ESP_OK);
    out = touch_sampler_diag_snapshot();
    assert(out.present && out.active && out.stats_valid && out.age_valid);
    assert(out.epoch == 1U && out.captured_us == 1002U && out.age_us == 7U);
    assert(out.samples == UINT64_MAX && out.poll_calls == UINT64_MAX-1U);
    assert(out.poll_total_us == UINT64_MAX-2U && out.poll_max_us == UINT32_MAX);
    assert(out.overflows == 3U && out.stale_neutralizations == 4U);
    assert(out.producer_stalls == 5U && out.recoveries == 6U);
    assert(out.last_completed_us == 995 && out.fault == ESP_ERR_TIMEOUT);

    /* Stop may free the handle. Retain the before-stop copy, not final totals. */
    touch_sampler_diag_capture();
    const uint64_t retained_at = s_touch_sampler_diag.captured_us;
    const unsigned calls_before_stop = getter_calls;
    s_touch_sampler = NULL;
    now_us = 9000;
    out = touch_sampler_diag_snapshot();
    assert(out.present && !out.active && out.stats_valid && out.age_valid);
    assert(out.captured_us == retained_at && out.age_us == retained_at - 995U);
    assert(out.fault == ESP_ERR_TIMEOUT && getter_calls == calls_before_stop);

    /* Failed start with no retained owner keeps the prior observation. */
    touch_sampler_diag_start_result(ESP_FAIL);
    out = touch_sampler_diag_snapshot();
    assert(out.epoch == 1U && out.captured_us == retained_at && !out.active);
    assert(s_touch_sampler_epoch == 1U && getter_calls == calls_before_stop);

    /* A new successful owner starts a new epoch and clears prior counters. */
    s_touch_sampler = &handle;
    memset(&provided, 0, sizeof(provided));
    now_us = 10000;
    touch_sampler_diag_start_result(ESP_OK);
    out = touch_sampler_diag_snapshot();
    assert(out.epoch == 2U && out.samples == 0U && out.fault == ESP_OK);
    assert(out.present && out.active && out.stats_valid && !out.age_valid);
    assert(out.age_us == 0U && out.last_completed_us == 0);

    /* A failed copied getter preserves its old timestamp rather than relabeling. */
    const uint64_t old_capture = out.captured_us;
    get_result = ESP_FAIL;
    now_us = 11000;
    out = touch_sampler_diag_snapshot();
    assert(out.captured_us == old_capture && out.stats_valid && out.active);

    get_result = ESP_OK;
    provided.last_completed_us = 20000;
    now_us = 12000;
    out = touch_sampler_diag_snapshot();
    assert(!out.age_valid && out.age_us == 0U); /* Clock inversion. */
    now_us = -10;
    out = touch_sampler_diag_snapshot();
    assert(out.captured_us == 0U && !out.age_valid && out.age_us == 0U);

    /* A retained teardown-only failed-start handle has no successful epoch. */
    now_us = 15000;
    get_result = ESP_FAIL;
    touch_sampler_diag_start_result(ESP_FAIL);
    out = touch_sampler_diag_snapshot();
    assert(out.present && out.active && !out.stats_valid && !out.age_valid);
    assert(out.epoch == 0U && s_touch_sampler_epoch == 2U);

    /* Normal retained startup-unwind handles still provide valid zero stats. */
    memset(&provided, 0, sizeof(provided));
    get_result = ESP_OK;
    touch_sampler_diag_start_result(ESP_FAIL);
    out = touch_sampler_diag_snapshot();
    assert(out.present && out.active && out.stats_valid && !out.age_valid);
    assert(out.epoch == 0U && s_touch_sampler_epoch == 2U);
    assert(out.samples == 0U && out.poll_calls == 0U && out.poll_total_us == 0U);
    assert(out.last_completed_us == 0 && out.age_us == 0U && out.fault == ESP_OK);

    /* Zero is reserved; wrap is explicit and paired with diagnostic generation. */
    s_touch_sampler_epoch = UINT32_MAX;
    get_result = ESP_OK;
    provided.last_completed_us = 0;
    touch_sampler_diag_start_result(ESP_OK);
    out = touch_sampler_diag_snapshot();
    assert(out.epoch == 1U && out.stats_valid && out.active);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='p4-touch-diag-owner-') as directory:
    folder = Path(directory)
    test = folder / 'test.c'
    test.write_text(HARNESS.replace('/* DIAG_CONTRACT */', contract).replace('/* PRODUCTION_HELPERS */', production))
    binary = folder / 'test'
    includes = [ROOT/'components/platform_touch/include', ROOT/'components/platform_touch/host/include', ROOT/'components/platform_board/include']
    subprocess.run([os.environ.get('CC','cc'), '-std=c11','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wshadow','-Werror', '-fsanitize=address,undefined','-fno-omit-frame-pointer', '-DCONFIG_P4_BOARD_M5STACK_TAB5=1', str(test), *[arg for p in includes for arg in ('-I',str(p))], '-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Touch sampler diagnostic owner: copied stats, age, epochs, failed starts, retained teardown and wiring passed')
