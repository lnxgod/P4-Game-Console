#!/usr/bin/env python3
"""Compile actual OS native lease launch/present/close paths with ASan/UBSan.

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

ROOT = Path(os.environ.get(
    "P4_CONSOLE_TEST_SOURCE_ROOT", Path(__file__).resolve().parents[2]
)).resolve()
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
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "p4/cartridge.h"
#include "p4/video_presenter.h"
#include "platform/display_worker_esp.h"
#define CONFIG_P4_BOARD_M5STACK_TAB5 1
#define CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 0
#define ESP_OK 0
#define ESP_ERR_TIMEOUT 0x107
#define CONSOLE_SUBMIT_TIMEOUT_MS 100U
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
    p4_cartridge_host_v1_t *host;
    p4_game_video_presenter_t *video_presenter;
    p4_game_video_stats_t video_stats;
    uint32_t display_ack_misses;
    int64_t poll_completed_us;
    struct { bool started; } frame_clock;
    phase_t render_phase, present_phase, display_reuse_phase;
    phase_t display_transform_phase, display_handoff_phase;
} cartridge_run_context_t;
struct p4_game_video_presenter { int token; } presenter;
static cartridge_run_context_t context;
static p4_cartridge_host_v1_t host;
static p4_game_video_stats_t model_stats;
static unsigned creates, stops, acquires, commits, cancels, sync_presents;
static unsigned sync_mode_logs, next_slot;
static int create_result, acquire_result, commit_result, cancel_result;
static int display_result, stop_result;
static bool lease_held, commit_timeout_consumed, hold_escape;
static uint16_t s_pixels[1], lease_pixels[2][8];
static uint16_t *held_pixels;
static char last_log[512];
static size_t last_stride;
static jmp_buf failed_join;
static void reset(void) {
    context=(cartridge_run_context_t){.game_id="test",.high_res_video=true};
    host=(p4_cartridge_host_v1_t){
        .surface={.pixels=s_pixels,.width=P4_GAME_SURFACE_HIGH_RES_WIDTH,
                  .height=P4_GAME_SURFACE_HIGH_RES_HEIGHT,
                  .stride_pixels=P4_GAME_SURFACE_HIGH_RES_WIDTH},.context=&context};
    model_stats=(p4_game_video_stats_t){0};
    creates=stops=acquires=commits=cancels=sync_presents=sync_mode_logs=next_slot=0;
    create_result=acquire_result=commit_result=cancel_result=stop_result=DW_OK;
    display_result=ESP_OK;
    lease_held=commit_timeout_consumed=hold_escape=false;
    held_pixels=NULL;
    last_stride=0;
    memset(lease_pixels,0,sizeof(lease_pixels));
    memset(last_log,0,sizeof(last_log));
}
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
static void vTaskDelay(uint32_t ticks) {
    assert(ticks==1000U && hold_escape);
    assert(context.video_presenter==&presenter && host.surface.pixels==NULL);
    longjmp(failed_join,1);
}
static void cartridge_phase_record(phase_t *p, uint64_t us) {
    ++p->samples;p->total+=us;
}
static int cartridge_video_backend(void *c,const uint16_t *p,size_t s,uint32_t t,
                                   p4_game_video_backend_metrics_t *m) {
    (void)c;(void)p;(void)s;(void)t;(void)m;return DW_OK;
}
int display_worker_esp_create(int (*s)(void *,const void *,size_t,uint32_t),
    void *c,size_t w,size_t h,size_t p,display_worker_t **o) {
    (void)s;(void)c;(void)w;(void)h;(void)p;(void)o;return DW_OK;
}
int p4_game_video_presenter_create(const p4_game_video_config_t *c,
                                  p4_game_video_presenter_t **out) {
    ++creates;assert(c->width==768U && c->height==480U && !*out);
    assert(c->backend_context==&context && c->backend_submit==cartridge_video_backend);
    assert(c->backend_timeout_error==ESP_ERR_TIMEOUT);
    assert(c->now_us==cartridge_video_now_us && c->worker_create==display_worker_esp_create);
    if (create_result==DW_OK) *out=&presenter;
    return create_result;
}
int p4_game_video_presenter_stats(p4_game_video_presenter_t *p,p4_game_video_stats_t *s) {
    assert(p==&presenter && s);*s=model_stats;return DW_OK;
}
int p4_game_video_presenter_acquire(p4_game_video_presenter_t *p,
    uint16_t **pixels,size_t *stride,uint32_t wait) {
    assert(p==&presenter && pixels && stride && wait==100U && !lease_held);
    if (acquires!=0U) assert(host.surface.pixels==NULL);
    ++acquires;
    *pixels=NULL;*stride=0;
    if (acquire_result==DW_OK) {
        held_pixels=lease_pixels[next_slot++%2U];
        lease_held=true;*pixels=held_pixels;*stride=768U;
    } else if (acquire_result==DW_TIMEOUT) ++model_stats.wait_timeouts;
    return acquire_result;
}
int p4_game_video_presenter_commit(p4_game_video_presenter_t *p,
    uint32_t wait,uint32_t submit) {
    assert(p==&presenter && wait==100U && submit==100U);
    assert(lease_held && host.surface.pixels==NULL);
    ++commits;
    if (commit_result==DW_OK ||
        (commit_result==DW_TIMEOUT && commit_timeout_consumed)) {
        lease_held=false;held_pixels=NULL;
        ++model_stats.accepted;
        if (commit_result==DW_OK) ++model_stats.completed;
    }
    if (commit_result==DW_TIMEOUT) ++model_stats.wait_timeouts;
    return commit_result;
}
int p4_game_video_presenter_cancel(p4_game_video_presenter_t *p) {
    assert(p==&presenter);++cancels;
    if (cancel_result==DW_OK) {lease_held=false;held_pixels=NULL;}
    return cancel_result;
}
int p4_game_video_presenter_stop(p4_game_video_presenter_t **p,uint32_t timeout,
                                p4_game_video_stats_t *stats) {
    assert(*p==&presenter && timeout==1000U && stats);
    assert(host.surface.pixels==NULL);++stops;
    lease_held=false;held_pixels=NULL;model_stats.closing=true;
    if (stop_result!=DW_OK) return stop_result;
    *stats=model_stats;*p=NULL;return DW_OK;
}
static int sync_submit(const uint16_t *p,size_t stride,uint32_t timeout) {
    assert(p==s_pixels && timeout==100U);++sync_presents;last_stride=stride;
    return display_result;
}
#define platform_display_submit_game_content_rgb565 sync_submit
#define platform_display_submit_rgb565 sync_submit
static int platform_display_get_stats(platform_display_stats_t *d) {
    *d=(platform_display_stats_t){1,2,3};return ESP_OK;
}
'''

TEST_MAIN = r'''
static void assert_closed(void) {
    assert(cartridge_video_close(&context)==DW_OK && !context.video_presenter);
    assert(!lease_held);
}
static void test_launch_and_present(void) {
    for (unsigned trial=0;trial<3U;++trial) {
        reset();
        create_result=trial==0U?DW_OK:(trial==1U?DW_NO_MEMORY:DW_INVALID);
        const bool opened=cartridge_video_open(&context,&host);
        if (!P4_CONSOLE_NATIVE_VIDEO_WORKER) {
            assert(opened && creates==0U && acquires==0U && !context.video_presenter);
            assert(sync_mode_logs==1U && host.surface.pixels==s_pixels);
        } else {
            assert(creates==1U && opened==(trial!=2U));
            assert((context.video_presenter!=NULL)==(trial==0U));
            assert(acquires==(trial==0U?1U:0U) && sync_mode_logs==0U);
        }
        if (!opened) {assert_closed();continue;}
        const bool worker=context.video_presenter!=NULL;
        if (worker) {
            assert(context.host==&host && host.surface.pixels==held_pixels);
            assert(host.surface.pixels!=s_pixels && host.surface.stride_pixels==768U);
            assert(strstr(last_log,"source=rotating-lease"));
        } else assert(host.surface.pixels==s_pixels);
        context.frame_clock.started=true;context.poll_completed_us=500;
        for (unsigned frame=0;frame<4U;++frame) {
            uint16_t *const previous=host.surface.pixels;
            previous[0]=(uint16_t)(0x100U+frame);
            assert(cartridge_present(&context));
            if (worker) {
                assert(host.surface.pixels!=previous && host.surface.pixels==held_pixels);
                assert(previous[0]==0x100U+frame);
            } else assert(host.surface.pixels==s_pixels && last_stride==768U);
        }
        assert(context.render_phase.samples==1U && context.present_phase.samples==4U);
        assert(context.display_transform_phase.samples==(worker?0U:4U));
        assert(commits==(worker?4U:0U) && acquires==(worker?5U:0U));
        assert(sync_presents==(worker?0U:4U));
        assert(cancels==0U);
        if (!worker) {
            context.high_res_video=false;assert(cartridge_present(&context));
            assert(last_stride==320U);
            display_result=ESP_ERR_TIMEOUT;assert(cartridge_present(&context));
            assert(context.display_ack_misses==1U && host.surface.pixels==s_pixels);
            display_result=-1;assert(!cartridge_present(&context));
        }
        assert_closed();assert(stops==(worker?1U:0U));
    }
}
static void test_initial_acquire_failure(void) {
    if (!P4_CONSOLE_NATIVE_VIDEO_WORKER) return;
    for (unsigned trial=0;trial<2U;++trial) {
        reset();acquire_result=trial==0U?DW_TIMEOUT:DW_INVALID;
        assert(!cartridge_video_open(&context,&host));
        assert(creates==1U && acquires==1U && stops==1U);
        assert(!context.video_presenter && host.surface.pixels==NULL && !lease_held);
        assert_closed();assert(stops==1U);
    }
}
static void test_commit_timeout_ownership(void) {
    if (!P4_CONSOLE_NATIVE_VIDEO_WORKER) return;
    for (unsigned consumed=0;consumed<2U;++consumed) {
        reset();assert(cartridge_video_open(&context,&host));
        uint16_t *const previous=host.surface.pixels;
        previous[0]=0x5678U;
        commit_result=DW_TIMEOUT;commit_timeout_consumed=consumed!=0U;
        assert(cartridge_present(&context));
        assert(commits==1U && cancels==1U && acquires==2U);
        assert(context.display_ack_misses==1U);
        assert(model_stats.accepted==consumed);
        assert(host.surface.pixels!=previous && host.surface.pixels==held_pixels);
        assert(previous[0]==0x5678U);
        assert_closed();
    }
}
static void test_next_acquire_failure(void) {
    if (!P4_CONSOLE_NATIVE_VIDEO_WORKER) return;
    for (unsigned trial=0;trial<2U;++trial) {
        reset();assert(cartridge_video_open(&context,&host));
        acquire_result=trial==0U?DW_TIMEOUT:DW_INVALID;
        assert(!cartridge_present(&context));
        assert(commits==1U && acquires==2U && !lease_held);
        assert(host.surface.pixels==NULL);
        assert_closed();assert(stops==1U);
    }
}
static void test_hard_error_and_cancel_failure(void) {
    if (!P4_CONSOLE_NATIVE_VIDEO_WORKER) return;
    reset();assert(cartridge_video_open(&context,&host));
    commit_result=DW_INVALID;
    assert(!cartridge_present(&context));
    assert(host.surface.pixels==NULL && commits==1U && acquires==1U);
    assert_closed();
    reset();assert(cartridge_video_open(&context,&host));
    commit_result=DW_TIMEOUT;cancel_result=DW_INVALID;
    assert(!cartridge_present(&context));
    assert(host.surface.pixels==NULL && cancels==1U && acquires==1U);
    assert_closed();
    reset();assert(cartridge_video_open(&context,&host));
    model_stats.hard_error=DW_INVALID;
    assert(!cartridge_present(&context));
    assert(host.surface.pixels==NULL);
    assert(cartridge_video_close(&context)==DW_INVALID);
    assert(!context.video_presenter && !lease_held && stops==1U);
}
static void test_join_failure_retains_owner(void) {
    if (!P4_CONSOLE_NATIVE_VIDEO_WORKER) return;
    reset();assert(cartridge_video_open(&context,&host));
    stop_result=DW_TIMEOUT;hold_escape=true;
    if (setjmp(failed_join)==0) {
        (void)cartridge_video_close(&context);
        assert(!"failed join must hold without releasing ownership");
    }
    assert(context.video_presenter==&presenter && host.surface.pixels==NULL);
    assert(stops==1U && !lease_held && model_stats.closing && sync_presents==0U);
    assert(strstr(last_log,"retain-context-display-free-hold"));
    stop_result=DW_OK;hold_escape=false;assert_closed();assert(stops==2U);
}
int main(void) {
    test_launch_and_present();
    test_initial_acquire_failure();
    test_commit_timeout_ownership();
    test_next_acquire_failure();
    test_hard_error_and_cancel_failure();
    test_join_failure_retains_owner();
    assert(!cartridge_present(NULL));
    puts("native direct-lease launch/present/close paths passed");return 0;
}
'''


class NativeVideoGateTests(unittest.TestCase):
    def compile_and_run(self, mode):
        functions = "\n".join(extract_function(n) for n in (
            "cartridge_video_snapshot", "cartridge_video_close",
            "cartridge_video_open", "cartridge_present"))
        with tempfile.TemporaryDirectory(prefix="native-video-gate-") as temp:
            source = Path(temp) / "gate.c"
            binary = Path(temp) / "gate"
            source.write_text(FIXTURE + "\n" + GATE.group() + "\n" + functions + TEST_MAIN)
            command = shlex.split(os.environ.get("CC", "cc")) + [
                "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer", str(source), "-o", str(binary)]
            for component in ("p4_game_api", "p4_game_platform", "platform_display"):
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

    def test_default_is_direct_lease_worker(self):
        self.assertRegex(GATE.group(), r"#define " + MACRO + r" 1\n")
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
