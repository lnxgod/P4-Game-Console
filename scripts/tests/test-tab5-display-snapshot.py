#!/usr/bin/env python3
"""Compile the production Tab5 scanout copy and retirement fence on the host.

The real frame queue is retained; semaphore and cache calls are hardware
boundaries. These tests do not open a device or build firmware.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "components/platform_display/src/platform_display_tab5.c"


def function(source, name):
    match = re.search(r"^(?:static\s+)?[\w\s*]+?\b" + re.escape(name)
                      + r"\s*\([^;{}]*?\)\s*\{", source, re.M)
    if match is None:
        raise ValueError(f"No definition for {name}")
    brace = match.end() - 1
    depth = 0
    for token in re.finditer(
            r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',
            source[brace:]):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():brace + token.end()]
    raise ValueError(f"Unclosed definition for {name}")


FIXTURE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tab5_frame_queue.h"
typedef int esp_err_t;
typedef uint32_t TickType_t;
typedef int SemaphoreHandle_t;
enum { ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, ESP_ERR_TIMEOUT,
       ESP_FAIL, pdTRUE=1, ESP_CACHE_MSYNC_FLAG_DIR_M2C=8 };
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms)/10U)
#define FRAME_BYTES ((size_t)720U*1280U*sizeof(uint16_t))
#define COPY_CACHE_CHUNK_BYTES ((size_t)32768U)
#define CACHE_CHUNKS ((FRAME_BYTES+COPY_CACHE_CHUNK_BYTES-1U)/COPY_CACHE_CHUNK_BYTES)
_Alignas(64) static uint16_t frames[TAB5_FRAME_COUNT][720U*1280U];
static uint16_t output[720U*1280U+16U];
static uint16_t *s_frames[TAB5_FRAME_COUNT]={frames[0],frames[1],frames[2]};
static tab5_frame_queue_t s_queue;
static bool s_frame_dma_clean[TAB5_FRAME_COUNT]={true,true,true};
static bool s_ready=true,s_pattern;
static SemaphoreHandle_t s_lock=1,s_refresh=2;
static struct {uint32_t submits_completed;} s_stats;
static uint32_t s_refresh_count;
static TickType_t ticks=100U,lock_elapsed,refresh_budget;
static unsigned take_calls,give_calls,refresh_calls,cache_calls,copy_calls;
static bool held,lock_timeout,refresh_timeout;
static unsigned cache_failure_chunk;
static TickType_t xTaskGetTickCount(void){return ticks;}
static int xSemaphoreTake(SemaphoreHandle_t semaphore,TickType_t budget)
{
    if(semaphore==s_lock) {
        assert(!held && budget==10U);
        ++take_calls;
        if(lock_timeout)return 0;
        ticks+=lock_elapsed;held=true;return pdTRUE;
    }
    assert(semaphore==s_refresh && held && s_queue.pending[s_queue.selected]);
    assert(cache_calls==0U && copy_calls==0U);
    assert(budget==10U-lock_elapsed-refresh_calls);
    ++refresh_calls;refresh_budget=budget;
    if(refresh_timeout)return 0;
    ++ticks;++s_refresh_count;return pdTRUE;
}
static int xSemaphoreGive(SemaphoreHandle_t semaphore)
{assert(semaphore==s_lock && held);held=false;++give_calls;return pdTRUE;}
static esp_err_t esp_cache_msync(void *source,size_t bytes,int flags)
{
    assert(held && !s_queue.pending[s_queue.selected] && s_queue.completed>0U);
    const size_t offset=(size_t)cache_calls*COPY_CACHE_CHUNK_BYTES;
    const size_t remaining=FRAME_BYTES-offset;
    assert(source==(uint8_t *)s_frames[s_queue.selected]+offset);
    assert((uintptr_t)source%64U==0U);
    assert(bytes==(remaining>COPY_CACHE_CHUNK_BYTES?COPY_CACHE_CHUNK_BYTES:remaining));
    assert(bytes<=32768U && bytes%64U==0U && offset%64U==0U);
    assert(flags==ESP_CACHE_MSYNC_FLAG_DIR_M2C && copy_calls==0U);
    ++cache_calls;
    if(cache_failure_chunk==cache_calls)return ESP_FAIL;
    /* Simulate the first and final DMA-written pixels becoming CPU-visible. */
    if(offset==0U)s_frames[s_queue.selected][0]=0x1234U;
    if(offset+bytes==FRAME_BYTES)s_frames[s_queue.selected][720U*1280U-1U]=0xabcdU;
    return ESP_OK;
}
static void *snapshot_memcpy(void *destination,const void *source,size_t bytes)
{
    assert(held && cache_calls==CACHE_CHUNKS && !cache_failure_chunk);
    assert(!s_queue.pending[s_queue.selected] && s_queue.completed>0U);
    /* Revocation precedes this read; cached pixels cannot later be written
     * through the DMA-only target path without an authoritative rebuild. */
    assert(!s_frame_dma_clean[s_queue.selected]);
    for(unsigned i=0U;i<TAB5_FRAME_COUNT;++i)
        if(i!=s_queue.selected)assert(s_frame_dma_clean[i]);
    assert(destination==output && source==s_frames[s_queue.selected] && bytes==1843200U);
    ++copy_calls;return memcpy(destination,source,bytes);
}
#define memcpy snapshot_memcpy
'''

CASES = r'''
#undef memcpy
static void no_pixels(void)
{
    assert(copy_calls==0U);
    for(unsigned i=0U;i<TAB5_FRAME_COUNT;++i)assert(s_frame_dma_clean[i]);
    for(size_t i=0;i<sizeof(output)/sizeof(output[0]);++i)assert(output[i]==0xbeefU);
}
static void released(void)
{assert(take_calls==1U && give_calls==1U && !held);}
int main(int argc,char **argv)
{
    assert(argc==2);
    for(unsigned frame=0;frame<TAB5_FRAME_COUNT;++frame)
        for(size_t i=0;i<720U*1280U;++i)frames[frame][i]=(uint16_t)(frame*1000U+i%997U);
    for(size_t i=0;i<sizeof(output)/sizeof(output[0]);++i)output[i]=0xbeefU;
    tab5_frame_queue_init(&s_queue);
    s_queue.selected=2U;s_queue.completed=1U;
    esp_err_t result;
    if(strcmp(argv[1],"arguments")==0) {
        assert(platform_display_copy_scanout_rgb565(NULL,FRAME_BYTES,100U)==ESP_ERR_INVALID_ARG);
        assert(platform_display_copy_scanout_rgb565(output,FRAME_BYTES-1U,100U)==ESP_ERR_INVALID_ARG);
        assert(take_calls==0U && give_calls==0U && cache_calls==0U);no_pixels();return 0;
    }
    if(strcmp(argv[1],"no-lock")==0) {
        s_lock=0;
        assert(platform_display_copy_scanout_rgb565(output,FRAME_BYTES,100U)==ESP_ERR_INVALID_STATE);
        assert(take_calls==0U && give_calls==0U && cache_calls==0U);no_pixels();return 0;
    }
    if(strcmp(argv[1],"lock-timeout")==0) {
        lock_timeout=true;
        assert(platform_display_copy_scanout_rgb565(output,FRAME_BYTES,100U)==ESP_ERR_TIMEOUT);
        assert(take_calls==1U && give_calls==0U && cache_calls==0U && !held);no_pixels();return 0;
    }
    if(strcmp(argv[1],"unready")==0)s_ready=false;
    else if(strcmp(argv[1],"pattern")==0)s_pattern=true;
    else if(strcmp(argv[1],"failed")==0)s_queue.failed=true;
    else if(strcmp(argv[1],"no-frame")==0)s_queue.completed=0U;
    else if(strcmp(argv[1],"cache-failure")==0)cache_failure_chunk=1U;
    else if(strcmp(argv[1],"cache-partial-failure")==0)cache_failure_chunk=3U;
    else if(strncmp(argv[1],"pending",7)==0) {
        s_queue.pending[2]=true;s_queue.published_at[2]=5U;s_refresh_count=5U;
        lock_elapsed=3U;
        if(strcmp(argv[1],"pending-timeout")==0)refresh_timeout=true;
        if(strcmp(argv[1],"pending-budget-exhausted")==0)lock_elapsed=10U;
    } else if(strncmp(argv[1],"copy-",5)==0)s_queue.selected=(unsigned)atoi(argv[1]+5);
    else assert(false);
    result=platform_display_copy_scanout_rgb565(output,sizeof(output),100U);
    released();
    if(!s_ready || s_pattern || s_queue.failed || s_queue.completed==0U) {
        assert(result==ESP_ERR_INVALID_STATE && cache_calls==0U);no_pixels();
    } else if(refresh_timeout || lock_elapsed==10U) {
        assert(result==ESP_ERR_TIMEOUT && cache_calls==0U);no_pixels();
        assert(refresh_calls==(refresh_timeout?1U:0U));
    } else if(cache_failure_chunk) {
        assert(result==ESP_FAIL && cache_calls==cache_failure_chunk);no_pixels();
    } else {
        assert(result==ESP_OK && cache_calls==CACHE_CHUNKS && copy_calls==1U);
        for(unsigned i=0U;i<TAB5_FRAME_COUNT;++i)
            assert(s_frame_dma_clean[i]==(i!=s_queue.selected));
        assert(output[0]==0x1234U && output[720U*1280U-1U]==0xabcdU);
        for(size_t i=1;i<720U*1280U-1U;++i)
            assert(output[i]==(uint16_t)(s_queue.selected*1000U+i%997U));
        for(size_t i=720U*1280U;i<sizeof(output)/sizeof(output[0]);++i)assert(output[i]==0xbeefU);
        if(strncmp(argv[1],"pending",7)==0)
            assert(refresh_calls==2U && refresh_budget==6U && s_queue.completed==2U);
        else assert(refresh_calls==0U);
    }
    return 0;
}
'''


class DisplaySnapshotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="p4-tab5-snapshot-")
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name)
        source = SOURCE.read_text()
        path = directory / "snapshot.c"
        path.write_text(FIXTURE + "\n".join(function(source, name) for name in
            ["finish_pending", "cache_chunks", "platform_display_copy_scanout_rgb565"]) + CASES)
        cls.executable = directory / "snapshot"
        command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                   "-I", str(ROOT / "components/platform_display/src"),
                   "-I", str(ROOT / "components/platform_display/include"),
                   str(path), "-o", str(cls.executable)]
        built = subprocess.run(command, capture_output=True, text=True)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)

    def run_case(self, name):
        result = subprocess.run([self.executable, name], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_invalid_buffers_are_rejected_before_lock(self):
        self.run_case("arguments")

    def test_missing_lock_is_not_taken(self):
        self.run_case("no-lock")

    def test_lock_timeout_does_not_unlock_unowned_lock(self):
        self.run_case("lock-timeout")

    def test_unready_pattern_failed_and_unaccepted_frames_are_not_copied(self):
        for case in ["unready", "pattern", "failed", "no-frame"]:
            with self.subTest(case=case):
                self.run_case(case)

    def test_selected_scanout_is_invalidated_and_copied_with_bounds_under_lock(self):
        for slot in range(3):
            with self.subTest(slot=slot):
                self.run_case(f"copy-{slot}")

    def test_pending_frame_retires_before_cache_and_copy(self):
        self.run_case("pending-complete")

    def test_pending_timeout_releases_lock_without_copy(self):
        self.run_case("pending-timeout")

    def test_lock_acquisition_consumes_same_timeout_budget_as_retirement(self):
        self.run_case("pending-budget-exhausted")

    def test_cache_failure_releases_lock_without_copy(self):
        for case in ["cache-failure", "cache-partial-failure"]:
            with self.subTest(case=case):
                self.run_case(case)


if __name__ == "__main__":
    unittest.main()
