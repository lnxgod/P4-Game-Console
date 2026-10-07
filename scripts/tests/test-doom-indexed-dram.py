#!/usr/bin/env python3
"""Focused extracted production boundary proof; no engine WAD/device/build needed.

Links the unchanged candidate I_InitGraphics, shutdown, screen copy and palette
converter against exact app reservation/loan/cleanup functions and composite
cleanup. Only SDK/hardware, zone allocator and engine entry are fixture boundaries.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(os.environ.get('P4_INDEXED_BASE', ROOT))
APP = ROOT / 'apps/doom_embedded_touch_audio/main/doom_embedded_touch_audio_main.c'
ENGINE = Path(os.environ.get('P4_INDEXED_ENGINE_SOURCE', ROOT / 'third_party/doomgeneric/doomgeneric/i_video.c'))


def function(source, name):
    match = re.search(r'^(?:static\s+)?[\w\s*]+?\b' + re.escape(name) + r'\s*\([^;{}]*?\)\s*\{', source, re.M)
    if not match:
        raise ValueError(name)
    brace = match.end() - 1
    depth = 0
    for token in re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', source[brace:]):
        if token.group() == '{': depth += 1
        elif token.group() == '}':
            depth -= 1
            if not depth: return source[match.start():brace + token.end()]
    raise ValueError('unclosed ' + name)


COMMON = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include "i_video.h"
#define DOOMGENERIC_RESX 320
#define DOOMGENERIC_RESY 200
#define PU_STATIC 1
void *Z_Malloc(int bytes, int tag, void *owner);
void Z_Free(void *p);
_Noreturn void I_Error(char *message, ...);
extern char *myargv[];
int M_CheckParmWithArgs(char *flag, int args);
void I_InitInput(void);
'''

APP_PREFIX = r'''
#define MALLOC_CAP_INTERNAL 1U
#define MALLOC_CAP_8BIT 2U
#define MALLOC_CAP_DMA 4U
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_STATE 2
typedef int esp_err_t;
static const char *TAG="fixture";
static bool alloc_fail, audio_fail, simulate_init_failure;
static unsigned allocations, frees, zone_allocations, zone_frees, input_calls, logs;
static size_t last_bytes;
static uint32_t last_caps;
static void *heap_pointer, *zone_pointer;
static char last_log[1024];
static unsigned heap_calls[3][2];
static bool s_cleanup_active, s_cleanup_complete;
static bool s_video_initialized, s_display_initialized, s_blob_registered;
static bool s_console_os_launch_active=true;
static void *s_shared_bus;
static uint32_t *s_overlay_buffer, *DG_ScreenBuffer;
static jmp_buf terminal_jump;
static void *heap_caps_malloc(size_t bytes, uint32_t caps) {
    assert(!heap_pointer); ++allocations; last_bytes=bytes; last_caps=caps;
    if(alloc_fail) return NULL;
    heap_pointer=malloc(bytes); assert(heap_pointer); memset(heap_pointer,0xa5,bytes); return heap_pointer;
}
static void heap_caps_free(void *p) { assert(p && p==heap_pointer); ++frees; free(p); heap_pointer=NULL; }
static size_t heap_caps_get_free_size(uint32_t caps) { assert(caps==3 || caps==7); ++heap_calls[0][caps==7]; return caps==3?171603U-(heap_pointer?64000U:0U):132103U-(heap_pointer?64000U:0U); }
static size_t heap_caps_get_largest_free_block(uint32_t caps) { assert(caps==3 || caps==7); ++heap_calls[1][caps==7]; return heap_pointer?1536:65536; }
static size_t heap_caps_get_minimum_free_size(uint32_t caps) { assert(caps==3 || caps==7); ++heap_calls[2][caps==7]; return caps==3?61000:40000; }
static void checked_log(const char *tag,const char *fmt,...) __attribute__((format(printf,2,3)));
static void checked_log(const char *tag,const char *fmt,...) {
    (void)tag; ++logs; va_list args;va_start(args,fmt);int n=vsnprintf(last_log,sizeof(last_log),fmt,args);va_end(args);assert(n>=0 && (size_t)n<sizeof(last_log));
}
#define ESP_LOGI checked_log
#define ESP_LOGW checked_log
#define ESP_LOGE checked_log
static bool release_audio(void) { return !audio_fail; }
static bool release_retained_touch(void) { return true; }
static int platform_i2c_shared_destroy(void **p) { *p=NULL;return 0; }
static int platform_display_set_brightness(unsigned x) { (void)x;return 0; }
static int doom_video_deinit(void) { return 0; }
static int platform_display_deinit(void) { return 0; }
static int platform_readonly_blob_unregister(void) { return 0; }
static const char *esp_err_to_name(int e) { return e?"fail":"ok"; }
#define pdMS_TO_TICKS(x) (x)
static void vTaskDelay(unsigned x) { (void)x;longjmp(terminal_jump,1); }
static _Noreturn void esp_restart(void) { longjmp(terminal_jump,2); }
void *Z_Malloc(int bytes, int tag, void *owner) { assert(!zone_pointer && bytes==64000 && tag==PU_STATIC && owner==NULL); ++zone_allocations;zone_pointer=malloc((size_t)bytes);assert(zone_pointer);return zone_pointer; }
void Z_Free(void *p) { assert(p && p==zone_pointer && p!=heap_pointer); ++zone_frees;free(p);zone_pointer=NULL; }
_Noreturn void I_Error(char *message,...) { (void)message;abort(); }
char *myargv[]={"doom","-gfxmode","rgba8888"};
int M_CheckParmWithArgs(char *flag,int args) { (void)args;return !strcmp(flag,"-gfxmode")?1:0; }
void I_InitInput(void) { ++input_calls; }
void fixture_palette(void);
void fixture_convert(uint32_t *out);
'''

TESTS = r'''
static void pixels(void) {
    byte snapshot[64000];uint32_t *out=malloc(64000U*sizeof(*out));assert(out);
    for(unsigned i=0;i<64000;++i)I_VideoBuffer[i]=(byte)((i*73U+19U)&255U);
    I_ReadScreen(snapshot);assert(!memcmp(snapshot,I_VideoBuffer,sizeof(snapshot)));
    fixture_palette();fixture_convert(out);
    for(unsigned i=0;i<64000;++i) { unsigned v=snapshot[i];assert(out[i]==((v<<16)|((255U-v)<<8)|(v^0x5aU))); }
    free(out);
}
int main(int argc,char **argv) {
    assert(argc==2);const char *test=argv[1];
#if P4_DOOM_INDEXED_DRAM_ENABLED
    if(!strcmp(test,"reserved-pointer")) {
        fixture_create();assert(I_VideoBuffer==heap_pointer && heap_pointer);
        assert(last_bytes==64000 && last_caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        assert(((uintptr_t)I_VideoBuffer & 3U)==0);
        assert(zone_allocations==0 && s_indexed_framebuffer_borrowed);pixels();fixture_quit();
        assert(frees==1 && zone_frees==0 && !I_VideoBuffer && s_cleanup_complete);
    } else if(!strcmp(test,"allocation-fallback")) {
        alloc_fail=true;fixture_create();assert(!heap_pointer && I_VideoBuffer==zone_pointer);pixels();fixture_quit();
        assert(allocations==1 && frees==0 && zone_allocations==1 && zone_frees==1);
    } else if(!strcmp(test,"live-cleanup-retains")) {
        fixture_create();void *borrowed=I_VideoBuffer;composite_cleanup();
        assert(!s_cleanup_complete && heap_pointer==borrowed && I_VideoBuffer==borrowed && frees==0);pixels();fixture_quit();assert(frees==1);
    } else if(!strcmp(test,"shutdown-idempotent")) {
        fixture_create();void *borrowed=heap_pointer;I_ShutdownGraphics();I_ShutdownGraphics();
        assert(!I_VideoBuffer && !s_indexed_framebuffer_borrowed && heap_pointer==borrowed && frees==0);
        I_InitGraphics();assert(I_VideoBuffer==borrowed && allocations==1);pixels();fixture_quit();assert(frees==1 && !zone_frees);
        I_ShutdownGraphics();composite_cleanup();assert(frees==1);
    } else if(!strcmp(test,"failure-before-consume")) {
        simulate_init_failure=true;assert(setjmp(terminal_jump)==0? (fixture_create(),0):1);
        assert(allocations==1 && frees==1 && !heap_pointer && !I_VideoBuffer && s_cleanup_complete);
    } else if(!strcmp(test,"failure-after-consume")) {
        fixture_create();if(!setjmp(terminal_jump))halt_dark("fixture-failure",ESP_ERR_NO_MEM);
        assert(!s_indexed_engine_live && frees==1 && !I_VideoBuffer && !heap_pointer && s_cleanup_complete);
    } else if(!strcmp(test,"partial-cleanup-retry")) {
        fixture_create();audio_fail=true;fixture_quit();assert(!s_cleanup_complete && frees==1);
        audio_fail=false;composite_cleanup();assert(s_cleanup_complete && frees==1 && zone_frees==0);
    } else if(!strcmp(test,"relaunch-reservation")) {
        fixture_create();pixels();fixture_quit();assert(frees==1);
        /* Production restarts on exit. Simulate the next boot's composite flags;
         * do not claim that the rest of Doom supports in-process relaunch. */
        s_cleanup_complete=false;fixture_create();assert(allocations==2 && I_VideoBuffer==heap_pointer);pixels();fixture_quit();assert(frees==2);
    } else if(!strcmp(test,"bounded-loan")) {
        indexed_framebuffer_reserve();assert(!DG_AllocIndexedFramebuffer(64000));
        s_indexed_engine_live=true;assert(!DG_AllocIndexedFramebuffer(63999));assert(!DG_AllocIndexedFramebuffer(64001));
        I_InitGraphics();void *p=I_VideoBuffer;assert(p==heap_pointer);assert(!DG_AllocIndexedFramebuffer(64000));
        indexed_framebuffer_reserve();assert(allocations==1);DG_ReleaseIndexedFramebuffer((byte *)p+1);assert(s_indexed_framebuffer_borrowed);
        fixture_quit();assert(frees==1);
    } else if(!strcmp(test,"heap-telemetry")) {
        fixture_create();assert(strstr(last_log,"stage=engine-ready"));
        assert(strstr(last_log,"bytes=64000") && strstr(last_log,"internal_free=107603") && strstr(last_log,"dma_free=68103"));
        assert(strstr(last_log,"internal_min=61000") && strstr(last_log,"dma_min=40000"));
        indexed_framebuffer_heap("runtime");fixture_quit();
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<2;++j)assert(heap_calls[i][j]>=6);
    } else abort();
#else
    assert(!strcmp(test,"default-zone"));
    assert(!DG_AllocIndexedFramebuffer(64000));DG_ReleaseIndexedFramebuffer(NULL);
    I_InitGraphics();assert(I_VideoBuffer==zone_pointer && !allocations);pixels();I_ShutdownGraphics();I_ShutdownGraphics();
    assert(zone_allocations==1 && zone_frees==1 && !I_VideoBuffer);
    I_InitGraphics();pixels();I_ShutdownGraphics();assert(zone_allocations==2 && zone_frees==2);
#endif
    assert(!heap_pointer && !zone_pointer);puts("PASS");return 0;
}
'''


class IndexedDramTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='p4-indexed-dram-')
        cls.build = Path(cls.temp.name)
        app=APP.read_text();engine=ENGINE.read_text()
        # Exact app lifecycle block retains the default-OFF and board gates.
        lifecycle=app[app.index('/* Measurement-only opt-in'):app.index('static doom_touch_input_t')]
        # These are unchanged production statements from the actual entry path.
        create=app[app.index('#if P4_DOOM_INDEXED_DRAM_ENABLED\n    indexed_framebuffer_reserve();'):app.index("    key_prevweapon = '[';",app.index('    doomgeneric_Create(engine_argc, engine_argv);'))]
        quit_start=app.index('        if (doomgeneric_QuitRequested()) {')
        quit_end=app.index('#ifdef P4_CONSOLE_OS_EMBEDDED',quit_start)
        terminal=app[quit_start+len('        if (doomgeneric_QuitRequested()) {'):quit_end]
        assert '_Noreturn void halt_dark' in app
        assert app.index('const bool sound_enabled = try_audio_enable();') < app.index('    indexed_framebuffer_reserve();') < app.index('    doomgeneric_Create(engine_argc, engine_argv);')
        assert app.index('        doomgeneric_Tick();') < quit_start
        assert 's_indexed_engine_live' not in function(app,'engine_exit_composite')
        body=APP_PREFIX+lifecycle+'\n'+function(app,'composite_cleanup')+'\n'+function(app,'halt_dark')+r'''
static void doomgeneric_Create(int argc,char **argv) {
    (void)argc;(void)argv;
#if P4_DOOM_INDEXED_DRAM_ENABLED
    assert(s_indexed_engine_live);
    if(simulate_init_failure)halt_dark("fixture-init",ESP_ERR_NO_MEM);
#endif
    I_InitGraphics();
}
static void verify_audio_start_or_safe_degrade(bool enabled) { (void)enabled; }
static void fixture_create(void) {
    int engine_argc=0;char **engine_argv=NULL;bool sound_enabled=true;
'''+create+'\n}\nstatic void fixture_quit(void) {\n'+terminal+'\ncomposite_cleanup();\n}\n'+TESTS
        video=COMMON+engine[engine.index('struct FB_BitField'):engine.index('int usemouse = 0;')]+'\nstatic struct color colors[256];\n#define P4_DOOM_XRGB_FASTPATH 1\nstatic uint32_t xrgb8888_palette[256];\nbyte *I_VideoBuffer=NULL;\nboolean screenvisible;\n'
        if 'static bool p4_indexed_framebuffer_owned;' in engine:
            video+='static bool p4_indexed_framebuffer_owned;\n'
            for name in ['DG_AllocIndexedFramebuffer','DG_ReleaseIndexedFramebuffer']:
                video+='__attribute__((weak))\n'+function(engine,name)+'\n'
            video+=function(engine,'I_AllocateVideoBuffer')+'\n'
        for name in ['I_InitGraphics','I_ShutdownGraphics','I_ReadScreen','cmap_to_fb']:
            # Existing upstream scaling comparison is signed/unsigned. Keep the
            # suppression local to that unmodified initialization function.
            if name=='I_InitGraphics':video+='#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wsign-compare"\n'
            video+=function(engine,name)+'\n'
            if name=='I_InitGraphics':video+='#pragma GCC diagnostic pop\n'
        video+=r'''
void fixture_palette(void) {
    for(unsigned i=0;i<256;++i) { colors[i]=(struct color){.r=i,.g=255U-i,.b=i^0x5aU};xrgb8888_palette[i]=(i<<16)|((255U-i)<<8)|(i^0x5aU); }
}
void fixture_convert(uint32_t *out) { for(unsigned y=0;y<200;++y)cmap_to_fb((byte *)(out+y*320),I_VideoBuffer+y*320,320); }
'''
        (cls.build/'video.c').write_text(video)
        (cls.build/'app.c').write_text(COMMON+body)
        includes=['-I'+str(ROOT/'third_party/doomgeneric/doomgeneric'),'-I'+str(BASE/'third_party/doomgeneric/doomgeneric')]
        common=['cc','-O2','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-fno-omit-frame-pointer',*includes]
        cls.binaries={}
        for label,defines in [('on',['-DP4_DOOM_INDEXED_DRAM_EXPERIMENT=1','-DP4_CONSOLE_OS_EMBEDDED=1','-DCONFIG_P4_BOARD_M5STACK_TAB5=1']),('off',['-DP4_CONSOLE_OS_EMBEDDED=1','-DCONFIG_P4_BOARD_M5STACK_TAB5=1']),('legacy',['-DP4_DOOM_INDEXED_DRAM_EXPERIMENT=1','-DP4_CONSOLE_OS_EMBEDDED=1','-DCONFIG_P4_BOARD_M5STACK_TAB5=0'])]:
            if os.environ.get('P4_INDEXED_ENGINE_SOURCE') and label!='on':continue
            binary=cls.build/label
            subprocess.run([*common,*defines,str(cls.build/'video.c'),str(cls.build/'app.c'),'-o',str(binary)],check=True)
            cls.binaries[label]=binary

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def run_case(self,case,variant='on'):
        result=subprocess.run([str(self.binaries[variant]),case],text=True,capture_output=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('PASS',result.stdout)

    def test_reserved_pointer_and_pixels(self):self.run_case('reserved-pointer')
    def test_allocation_failure_uses_zone(self):self.run_case('allocation-fallback')
    def test_live_engine_retains_allocation(self):self.run_case('live-cleanup-retains')
    def test_shutdown_detaches_idempotently(self):self.run_case('shutdown-idempotent')
    def test_failure_before_engine_consumption(self):self.run_case('failure-before-consume')
    def test_failure_after_engine_consumption(self):self.run_case('failure-after-consume')
    def test_partial_cleanup_retry_does_not_double_free(self):self.run_case('partial-cleanup-retry')
    def test_relaunch_can_reserve_again(self):self.run_case('relaunch-reservation')
    def test_loan_is_exact_and_single(self):self.run_case('bounded-loan')
    def test_heap_telemetry_includes_both_capability_classes(self):self.run_case('heap-telemetry')
    def test_default_is_zone_backed(self):self.run_case('default-zone','off')
    def test_non_tab5_remains_zone_backed(self):self.run_case('default-zone','legacy')

if __name__=='__main__':unittest.main(verbosity=2)
