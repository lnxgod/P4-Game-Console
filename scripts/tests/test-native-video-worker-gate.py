#!/usr/bin/env python3
"""Compile the actual native launch/present/close paths with worker on and off.

P4_CONSOLE_BASE supplies unchanged headers when testing a sparse source overlay.
Only external services are stubbed; no serial transport or firmware build runs.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(os.environ.get("P4_CONSOLE_BASE", ROOT)).resolve()
SOURCE = (ROOT / "apps/console_os/main/console_os_main.c").read_text()
MACRO = "P4_CONSOLE_NATIVE_VIDEO_WORKER"


def extract_function(name):
    match = re.search(r"^static [^\n]+\b" + name + r"\([^;]+?\n\{", SOURCE, re.M)
    if not match:
        raise AssertionError(f"Missing actual function: {name}")
    # These functions use a column-zero closing brace only at function end.
    end = SOURCE.index("\n}", match.end()) + 2
    return SOURCE[match.start():end]


GATE = re.search(
    r"#ifndef " + MACRO + r"\n.*?#endif\n#if " + MACRO + r".*?\n#endif",
    SOURCE, re.S,
)
if GATE is None:
    raise AssertionError("Missing bounded native worker default gate")

FIXTURE = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "p4/video_presenter.h"
#include "platform/display_worker_esp.h"
#define CONFIG_P4_BOARD_M5STACK_TAB5 1
#define CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 0
#define ESP_OK 0
#define ESP_ERR_TIMEOUT 0x107
#define CONSOLE_SUBMIT_TIMEOUT_MS 100U
#define P4_GAME_SURFACE_WIDTH 320U
#define P4_GAME_SURFACE_HIGH_RES_WIDTH 768U
#define TAG "test"
#define pdMS_TO_TICKS(x) (x)
typedef int esp_err_t;
typedef struct { unsigned samples; uint64_t total; } phase_t;
typedef struct {
    uint32_t pipeline_reuse_wait_last_us, pipeline_transform_last_us;
    uint32_t pipeline_handoff_last_us;
} platform_display_stats_t;
typedef struct {
    const char *game_id;
    bool high_res_video;
    p4_game_video_presenter_t *video_presenter;
    p4_game_video_stats_t video_stats;
    uint32_t display_ack_misses;
    int64_t poll_completed_us;
    struct { bool started; } frame_clock;
    phase_t render_phase, present_phase, display_reuse_phase;
    phase_t display_transform_phase, display_handoff_phase;
} cartridge_run_context_t;
struct p4_game_video_presenter { int token; } presenter;
static unsigned creates, stops, worker_presents, sync_presents, sync_mode_logs;
static int create_result, present_result, display_result;
static uint16_t s_pixels[1];
static char last_log[512];
static size_t last_stride;
static void log_message(const char *tag, const char *format, ...) {
    (void)tag;
    va_list args; va_start(args,format);
    vsnprintf(last_log,sizeof(last_log),format,args); va_end(args);
    if (strstr(last_log,"mode=synchronous reason=native-worker-disabled"))
        ++sync_mode_logs;
}
#define ESP_LOGI(...) log_message(__VA_ARGS__)
#define ESP_LOGW(...) log_message(__VA_ARGS__)
#define ESP_LOGE(...) log_message(__VA_ARGS__)
static int64_t esp_timer_get_time(void) { return 1000; }
static uint64_t cartridge_video_now_us(void) { return 1000U; }
static int xPortGetCoreID(void) { return 0; }
static void vTaskDelay(uint32_t ticks) { (void)ticks; assert(!"unexpected join failure"); }
static void cartridge_phase_record(phase_t *p, uint64_t us) { ++p->samples; p->total+=us; }
static int cartridge_video_backend(void *c,const uint16_t *p,size_t s,uint32_t t,
                                   p4_game_video_backend_metrics_t *m) {
    (void)c;(void)p;(void)s;(void)t;(void)m; return DW_OK;
}
int display_worker_esp_create(int (*s)(void *,const void *,size_t,uint32_t),
    void *c,size_t w,size_t h,size_t p,display_worker_t **o) {
    (void)s;(void)c;(void)w;(void)h;(void)p;(void)o; return DW_OK;
}
int p4_game_video_presenter_create(const p4_game_video_config_t *c,
                                  p4_game_video_presenter_t **out) {
    ++creates; assert(c->width==768U && c->height==480U && !*out);
    assert(c->backend_context && c->backend_submit==cartridge_video_backend);
    assert(c->backend_timeout_error==ESP_ERR_TIMEOUT);
    assert(c->now_us==cartridge_video_now_us && c->worker_create==display_worker_esp_create);
    if (create_result==DW_OK) *out=&presenter;
    return create_result;
}
int p4_game_video_presenter_stats(p4_game_video_presenter_t *p,p4_game_video_stats_t *s) {
    assert(p==&presenter); (void)s; return DW_OK;
}
int p4_game_video_presenter_present(p4_game_video_presenter_t *p,const uint16_t *s,
                                   size_t stride,uint32_t wait,uint32_t submit) {
    assert(p==&presenter && s==s_pixels && wait==100U && submit==100U);
    ++worker_presents;last_stride=stride;return present_result;
}
int p4_game_video_presenter_stop(p4_game_video_presenter_t **p,uint32_t timeout,
                                p4_game_video_stats_t *stats) {
    assert(*p==&presenter && timeout==1000U && stats); ++stops;*p=NULL;return DW_OK;
}
static int sync_submit(const uint16_t *p,size_t stride,uint32_t timeout) {
    assert(p==s_pixels && timeout==100U); ++sync_presents;last_stride=stride;
    return display_result;
}
#define platform_display_submit_game_content_rgb565 sync_submit
#define platform_display_submit_rgb565 sync_submit
static int platform_display_get_stats(platform_display_stats_t *d) {
    *d=(platform_display_stats_t){1,2,3};return ESP_OK;
}
'''

TEST_MAIN = r'''
int main(void) {
    for (unsigned trial=0;trial<3U;++trial) {
        cartridge_run_context_t c={.game_id="test",.high_res_video=true};
        const unsigned before=creates;
        create_result=trial==0U?DW_OK:(trial==1U?DW_NO_MEMORY:DW_INVALID);
        const bool opened=cartridge_video_open(&c,768U,480U);
        if (!P4_CONSOLE_NATIVE_VIDEO_WORKER) {
            assert(opened && creates==before && !c.video_presenter);
            assert(sync_mode_logs==trial+1U);
        } else {
            assert(creates==before+1U && opened==(trial!=2U));
            assert((c.video_presenter!=NULL)==(trial==0U));
            assert(sync_mode_logs==0U);
        }
        if (!opened) { assert(cartridge_video_close(&c)==DW_OK);continue; }
        const bool worker=c.video_presenter!=NULL;
        const unsigned old_worker=worker_presents, old_sync=sync_presents, old_stops=stops;
        c.frame_clock.started=true;c.poll_completed_us=500;
        for (unsigned resolution=0;resolution<2U;++resolution) {
            c.high_res_video=resolution==0U;present_result=DW_OK;display_result=ESP_OK;
            assert(cartridge_present(&c));
            assert(last_stride==(c.high_res_video?768U:320U));
        }
        assert(c.render_phase.samples==1U && c.present_phase.samples==2U);
        assert(c.display_transform_phase.samples==(worker?0U:2U));
        assert(worker_presents-old_worker==(worker?2U:0U));
        assert(sync_presents-old_sync==(worker?0U:2U));
        present_result=DW_TIMEOUT;display_result=ESP_ERR_TIMEOUT;
        assert(cartridge_present(&c));
        if (!worker) assert(c.display_ack_misses==1U);
        present_result=DW_INVALID;display_result=-1;
        assert(!cartridge_present(&c));
        assert(cartridge_video_close(&c)==DW_OK && !c.video_presenter);
        assert(stops-old_stops==(worker?1U:0U));
    }
    assert(!cartridge_present(NULL));
    puts("native worker launch/present/close paths passed");return 0;
}
'''


class NativeVideoGateTests(unittest.TestCase):
    def compile_and_run(self, mode):
        functions = "\n".join(extract_function(n) for n in (
            "cartridge_video_snapshot", "cartridge_video_open",
            "cartridge_video_close", "cartridge_present"))
        with tempfile.TemporaryDirectory(prefix="native-video-gate-") as temp:
            source = Path(temp) / "gate.c"
            binary = Path(temp) / "gate"
            source.write_text(FIXTURE + "\n" + GATE.group() + "\n" + functions + TEST_MAIN)
            command = shlex.split(os.environ.get("CC", "cc")) + [
                "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer", str(source), "-o", str(binary)]
            for component in ("p4_game_platform", "platform_display"):
                relative = Path("components") / component / "include"
                command += ["-I", str(ROOT / relative), "-I", str(BASE / relative)]
            if mode is not None:
                command += [f"-D{MACRO}={mode}"]
            compiled = subprocess.run(command, capture_output=True, text=True)
            if mode not in (None, 0, 1):
                self.assertNotEqual(compiled.returncode, 0)
                self.assertIn("must be 0 or 1", compiled.stderr)
                return
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_default_is_disabled(self):
        self.assertRegex(GATE.group(), r"#define " + MACRO + r" 0\n")
        self.compile_and_run(None)

    def test_explicit_modes(self):
        for mode in (0, 1):
            with self.subTest(mode=mode):
                self.compile_and_run(mode)

    def test_rejects_unbounded_values(self):
        for mode in (-1, 2):
            with self.subTest(mode=mode):
                self.compile_and_run(mode)


if __name__ == "__main__":
    unittest.main()
