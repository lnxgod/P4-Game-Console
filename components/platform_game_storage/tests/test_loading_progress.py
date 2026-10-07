#!/usr/bin/env python3
"""Run the production Arena validation/progress control flow with small files.

Real stdio/stat and the unchanged production scan/snapshot functions are used.
Hash and WAD validators are controlled boundaries: their existing suites own
cryptographic/format correctness, while this checks when progress may advance
or become complete, including failed reads, hashes, and structure validation.
"""
from pathlib import Path
import importlib.util
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location(
    "loading_boundary", ROOT / "scripts/tests/test-console-doom-loading-boundary.py")
extract = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extract)

FIXTURE = r'''
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "platform/game_storage.h"
#include "game_storage_model.h"
#include "verified_reader.h"
#include "verified_wad.h"
#define CONFIG_P4_BOARD_M5STACK_TAB5 0
#define TAG "test-storage-progress"
static void fixture_log(const char *tag,const char *format,...);
#define ESP_LOGI(...) fixture_log(__VA_ARGS__)
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
enum { ARENA_WAD_COUNT=3 };
typedef struct { FILE *stream; } p4_storage_cursor_t;
typedef struct { unsigned updates; } mbedtls_sha256_context;
static bool s_initialized=true;
static game_storage_model_t s_model;
static platform_game_storage_doom_load_progress_t s_doom_load_progress;
static FILE *s_arena_file[ARENA_WAD_COUNT];
static p4_storage_cursor_t s_arena_cursor[ARENA_WAD_COUNT];
static uint8_t *s_arena_hashes[ARENA_WAD_COUNT];
static p4_verified_reader_t s_arena_reader[ARENA_WAD_COUNT];
static uint8_t *s_arena_base_pending,*s_arena_base_resident;
static size_t s_arena_base_resident_bytes;
typedef struct { const uint8_t *data; size_t bytes; } arena_memory_span_t;
typedef struct { size_t total_free_bytes,largest_free_block,minimum_free_bytes; } multi_heap_info_t;
static struct { const char *path; size_t bytes; const char *sha256; }
    s_arena_content[P4_GCA_FILE_COUNT];
static uint8_t s_hash_buffer[P4_VERIFIED_BLOCK_BYTES];
static uint8_t *s_locked_wad_data,*s_locked_deh_data;
static size_t s_locked_wad_size_bytes,s_locked_deh_size_bytes;
static platform_game_storage_doom_title_t s_locked_snapshot_title;
static const uint8_t s_expected_doom_sha256[32],s_expected_chex_sha256[32],s_expected_chex_deh_sha256[32];
static unsigned read_calls,digest_calls,finish_calls,structure_calls,callback_calls;
static unsigned fail_read,fail_digest,fail_identity,fail_structure;
static uint64_t hashed_bytes,total_bytes;
static uint64_t logged_read_max_us;
static unsigned logged_read_calls,logged_read_10ms,logged_read_100ms,logged_read_1s,logged_read_errors;
static int64_t fixture_clock;
static bool saw_structure,saw_complete;
static platform_game_storage_doom_load_progress_t latest;
static bool lock_storage(void) { return true; }
static void unlock_storage(void) {}
static int64_t esp_timer_get_time(void) { return ++fixture_clock; }
static void fixture_log(const char *tag,const char *format,...) {
    (void)tag;
    if(strstr(format,"SCAN_COST")){
        va_list args;va_start(args,format);
        for(unsigned i=0;i<3;++i)(void)va_arg(args,unsigned);
        (void)va_arg(args,uint64_t);
        logged_read_calls+=va_arg(args,unsigned);
        (void)va_arg(args,uint64_t);
        const uint64_t maximum=va_arg(args,uint64_t);
        if(maximum>logged_read_max_us)logged_read_max_us=maximum;
        logged_read_10ms+=va_arg(args,unsigned);
        logged_read_100ms+=va_arg(args,unsigned);
        logged_read_1s+=va_arg(args,unsigned);
        logged_read_errors+=va_arg(args,unsigned);
        va_end(args);
    }
}
static void *heap_caps_malloc(size_t size,unsigned caps) { (void)caps;return malloc(size); }
static void heap_caps_free(void *data) { free(data); }
static void heap_caps_get_info(multi_heap_info_t *info,unsigned caps) {
    (void)info;(void)caps;assert(0 && "Disabled BASE residency must not query PSRAM");
}
static size_t heap_caps_get_total_size(unsigned caps) {
    (void)caps;assert(0 && "Disabled BASE residency must not query PSRAM");return 0;
}
static void content_validation_note_bytes(size_t size) { (void)size; }
static void mbedtls_sha256_init(mbedtls_sha256_context *sha) { memset(sha,0,sizeof(*sha)); }
static int mbedtls_sha256_starts(mbedtls_sha256_context *sha,int kind) { (void)sha;(void)kind;return 0; }
static int mbedtls_sha256_update(mbedtls_sha256_context *sha,const void *data,size_t size) {
    (void)data;++sha->updates;hashed_bytes+=size;return 0;
}
static int mbedtls_sha256_finish(mbedtls_sha256_context *sha,uint8_t *digest) {
    (void)sha;memset(digest,0,32);if(++finish_calls==fail_identity)digest[0]=1;return 0;
}
static void mbedtls_sha256_free(mbedtls_sha256_context *sha) { (void)sha; }
static bool arena_digest(const void *data,size_t size,uint8_t *digest) {
    (void)data;(void)size;memset(digest,0,32);return ++digest_calls!=fail_digest;
}
static bool p4_storage_cursor_read_batch(void *context,size_t offset,void *out,size_t bytes) {
    p4_storage_cursor_t *cursor=context;
    ++read_calls;
    /* Deterministic stalls verify the actual startup diagnostics classify
     * each read, including failed I/O, without adding timers or test hooks. */
    if(read_calls==1)fixture_clock+=1100000;
    else if(read_calls==2)fixture_clock+=110000;
    else if(read_calls==3)fixture_clock+=11000;
    if(read_calls==fail_read)return false;
    return fseek(cursor->stream,(long)offset,SEEK_SET)==0&&fread(out,1,bytes,cursor->stream)==bytes;
}
static bool arena_read_block(void *context,size_t offset,void *out,size_t bytes) {
    return p4_storage_cursor_read_batch(context,offset,out,bytes);
}
bool p4_verified_wad_validate_with_progress(p4_verified_reader_t *reader,p4_wad_kind_t kind,
    void (*progress)(void *),void *context) {
    (void)reader;(void)kind;++structure_calls;
    assert(s_doom_load_progress.active&&s_doom_load_progress.checking_structure);
    assert(!s_doom_load_progress.complete);
    if(progress)progress(context);
    return structure_calls!=fail_structure;
}
static void release_locked_doom_snapshot(void) {
    for(unsigned i=0;i<ARENA_WAD_COUNT;++i){
        if(s_arena_file[i])fclose(s_arena_file[i]);s_arena_file[i]=NULL;
        free(s_arena_hashes[i]);s_arena_hashes[i]=NULL;
    }
    free(s_locked_wad_data);free(s_locked_deh_data);
    s_locked_wad_data=s_locked_deh_data=NULL;
    s_locked_wad_size_bytes=s_locked_deh_size_bytes=0;
}
static game_storage_content_t inspect_exact_file(const char *path,uint64_t size,
    const uint8_t *hash,bool wad,bool chex,esp_err_t *error,uint8_t **data,
    platform_game_storage_progress_fn_t progress,void *context) {
    (void)path;(void)size;(void)hash;(void)wad;(void)chex;(void)data;(void)progress;(void)context;
    *error=ESP_OK;return GAME_STORAGE_CONTENT_READY;
}
'''

CASES = r'''
static void progress(void *context) {
    assert(context==(void *)&callback_calls);
    ++callback_calls;
    assert(platform_game_storage_get_doom_load_progress(&latest)==ESP_OK);
    assert(latest.bytes_total==total_bytes&&latest.bytes_checked==hashed_bytes);
    assert(latest.bytes_checked<=latest.bytes_total&&latest.file_count==P4_GCA_FILE_COUNT);
    if(latest.checking_structure){saw_structure=true;assert(latest.active&&!latest.complete);}
    if(latest.complete){
        saw_complete=true;
        assert(!latest.active&&!latest.checking_structure);
        assert(latest.bytes_checked==total_bytes);
        assert(structure_calls==ARENA_WAD_COUNT&&finish_calls==P4_GCA_FILE_COUNT);
    }
}
int main(int argc,char **argv) {
    assert(argc==2);
    assert(!P4_ARENA_BASE_PSRAM_ENABLED);
    char names[P4_GCA_FILE_COUNT][32];
    for(unsigned i=0;i<P4_GCA_FILE_COUNT;++i){
        snprintf(names[i],sizeof(names[i]),"small-file-%u",i);
        s_arena_content[i].path=names[i];s_arena_content[i].bytes=4103U+i;
        s_arena_content[i].sha256="0000000000000000000000000000000000000000000000000000000000000000";
        total_bytes+=s_arena_content[i].bytes;
        FILE *file=fopen(names[i],"wb");assert(file);
        for(size_t n=0;n<s_arena_content[i].bytes;++n)assert(fputc((int)(n&255U),file)!=EOF);
        assert(fclose(file)==0);
    }
    if(!strcmp(argv[1],"read-failure"))fail_read=2;
    else if(!strcmp(argv[1],"digest-failure"))fail_digest=2;
    else if(!strcmp(argv[1],"identity-failure"))fail_identity=1;
    else if(!strcmp(argv[1],"structure-failure"))fail_structure=1;
    else if(!strcmp(argv[1],"missing"))assert(unlink(names[0])==0);
    else assert(!strcmp(argv[1],"success"));
    esp_err_t error=ESP_OK;
    const game_storage_content_t result=inspect_doom_title_snapshot(
        PLATFORM_GAME_STORAGE_DOOM_TITLE_GAME_CHANGERS_AI,PLATFORM_GAME_STORAGE_VERIFY_FULL_CONTENT,&error,progress,&callback_calls);
    assert(!latest.active&&!latest.checking_structure&&callback_calls>=2);
    assert(logged_read_calls==read_calls);
    assert(logged_read_max_us==(read_calls?1100001U:0U));
    assert(logged_read_10ms==(read_calls<3U?read_calls:3U));
    assert(logged_read_100ms==(read_calls<2U?read_calls:2U));
    assert(logged_read_1s==(read_calls?1U:0U));
    assert(logged_read_errors==(fail_read?1U:0U));
    if(!strcmp(argv[1],"success")){
        assert(result==GAME_STORAGE_CONTENT_READY&&error==ESP_OK&&saw_complete&&saw_structure);
        /* An unrelated title cannot inherit stale Arena success or counters. */
        assert(inspect_doom_title_snapshot(PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM,
            PLATFORM_GAME_STORAGE_VERIFY_FULL_CONTENT,&error,NULL,NULL)==GAME_STORAGE_CONTENT_READY);
        assert(platform_game_storage_get_doom_load_progress(&latest)==ESP_OK);
        assert(!latest.complete&&!latest.active&&latest.bytes_total==0);
    }else{
        assert(result!=GAME_STORAGE_CONTENT_READY&&error!=ESP_OK&&!saw_complete&&!latest.complete);
        if(fail_read||fail_digest)assert(latest.bytes_checked==P4_VERIFIED_BLOCK_BYTES);
        if(fail_identity||fail_structure)assert(latest.bytes_checked==s_arena_content[0].bytes);
        if(!strcmp(argv[1],"missing"))assert(latest.bytes_checked==0);
    }
    assert(platform_game_storage_get_doom_load_progress(NULL)==ESP_ERR_INVALID_ARG);
    s_initialized=false;memset(&latest,255,sizeof(latest));
    assert(platform_game_storage_get_doom_load_progress(&latest)==ESP_ERR_INVALID_STATE);
    assert(!latest.active&&!latest.complete&&latest.bytes_total==0);
    release_locked_doom_snapshot();
    assert(!s_arena_base_pending&&!s_arena_base_resident&&!s_arena_base_resident_bytes);
    for(unsigned i=0;i<P4_GCA_FILE_COUNT;++i)(void)unlink(names[i]);
    return 0;
}
'''


class LoadingProgressTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="p4-storage-progress-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        source = (ROOT / "components/platform_game_storage/src/platform_game_storage.c").read_text()
        units = [extract.function(source, name) for name in (
            "arena_psram_observe", "arena_psram_has_reserve", "arena_base_snapshot_begin",
            "arena_base_snapshot_recheck", "arena_read_memory",
            "platform_game_storage_get_doom_load_progress", "inspect_arena_structure", "inspect_arena_stream",
            "inspect_doom_title_snapshot")]
        gates = source[source.index("#ifndef P4_ARENA_BASE_PSRAM"):
                       source.index("#define P4_GAME_STORAGE_SD_BACKEND")]
        (cls.directory / "progress.c").write_text(FIXTURE + gates + "\n".join(units) + CASES)
        (cls.directory / "esp_err.h").write_text(
            "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n" +
            "\n".join(f"#define {name} {i+1}" for i, name in enumerate((
                "ESP_ERR_INVALID_ARG", "ESP_ERR_INVALID_STATE", "ESP_ERR_NOT_FOUND",
                "ESP_ERR_INVALID_SIZE", "ESP_ERR_NO_MEM", "ESP_ERR_INVALID_CRC",
                "ESP_ERR_INVALID_RESPONSE"))))
        cls.exe = cls.directory / "progress"
        command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-Wno-unused-variable", "-fsanitize=address,undefined", "-I", str(cls.directory),
                   "-I", str(ROOT / "components/platform_game_storage/include"),
                   "-I", str(ROOT / "components/platform_game_storage/src"),
                   str(cls.directory / "progress.c"), "-o", str(cls.exe)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def run_case(self, name):
        result = subprocess.run([self.exe, name], cwd=self.directory, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_success_requires_every_hash_and_structure(self): self.run_case("success")
    def test_read_failure_does_not_advance_checked_bytes(self): self.run_case("read-failure")
    def test_digest_failure_does_not_advance_checked_bytes(self): self.run_case("digest-failure")
    def test_failed_file_identity_never_completes(self): self.run_case("identity-failure")
    def test_failed_structure_never_completes(self): self.run_case("structure-failure")
    def test_missing_file_never_completes(self): self.run_case("missing")


if __name__ == "__main__":
    unittest.main()
