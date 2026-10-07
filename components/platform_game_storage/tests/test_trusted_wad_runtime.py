#!/usr/bin/env python3
"""Actual Arena preparation/cleanup with exact pinned inputs and real verifiers.

Only hardware allocation, timer and device-path routing are substituted. Full
and lazy preparation, the public policy wrappers, descriptor validation, SHA,
block cursor/reader, WAD structural checks and cleanup are production code.
The production default leaves optional BASE residency off; these cases assert
that its merged helpers neither retain a snapshot nor query PSRAM admission.
"""
from pathlib import Path
import hashlib, importlib.util, json, os, shutil, subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[3]
INPUT_ROOT=Path(os.environ.get('P4_TRUSTED_INPUT_ROOT',ROOT)).resolve()
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
EXTRACT=module('loading_boundary_runtime',ROOT/'scripts/tests/test-console-doom-loading-boundary.py')
GEN=module('trusted_runtime_generator',ROOT/'scripts/doom/arena-trusted-digests.py')
FIXTURE=r'''
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "platform/game_storage.h"
#include "game_storage_model.h"
#include "storage_block_read.h"
#include "verified_reader.h"
#include "verified_wad.h"
#include "trusted_wad_table.h"
#include "sha256.h"
#define P4_GAME_STORAGE_BACKGROUND_CONTENT_SCAN 0
#define P4_GAME_STORAGE_USB_EXPORT 0
#define MALLOC_CAP_SPIRAM 0U
#define MALLOC_CAP_8BIT 0U
#define TAG "runtime-proof"
static void fixture_log(const char *tag,const char *format,...) {(void)tag;(void)format;}
#define ESP_LOGI(...) fixture_log(__VA_ARGS__)
enum { ARENA_WAD_COUNT=3 };
static bool s_initialized=true;
static game_storage_model_t s_model;
static esp_err_t s_last_error;
static game_storage_content_t s_doom_content,s_chex_content;
static platform_game_storage_doom_load_progress_t s_doom_load_progress;
static FILE *s_arena_file[3];
static p4_storage_cursor_t s_arena_cursor[3];
static uint8_t *s_arena_hashes[3];
static p4_verified_reader_t s_arena_reader[3];
static uint8_t *s_arena_base_pending,*s_arena_base_resident;
static size_t s_arena_base_resident_bytes;
typedef struct { const uint8_t *data; size_t bytes; } arena_memory_span_t;
typedef struct { size_t total_free_bytes,largest_free_block,minimum_free_bytes; } multi_heap_info_t;
static uint8_t s_hash_buffer[P4_VERIFIED_BLOCK_BYTES];
static uint8_t *s_locked_wad_data,*s_locked_deh_data;
static size_t s_locked_wad_size_bytes,s_locked_deh_size_bytes;
static platform_game_storage_doom_title_t s_locked_snapshot_title;
static const uint8_t s_expected_doom_sha256[32],s_expected_chex_sha256[32],s_expected_chex_deh_sha256[32];
static const struct { const char *path; size_t bytes; const char *sha256; }
    s_arena_content[P4_GCA_FILE_COUNT]={
#define GCA_STORAGE(kind,symbol,dir,name,size,hash) {"/game-data" dir "/" name,size,hash},
P4_GCA_CONTENT_FILES(GCA_STORAGE)
#undef GCA_STORAGE
};
static const char *override_path;
static unsigned override_index;
static unsigned file_opens,file_closes,allocations,frees,live_files,live_allocations;
static unsigned fail_allocation,allocation_attempts,callbacks,metadata_callbacks,structure_callbacks;
static unsigned heap_observations;
static bool fail_batch;
static FILE *file_registry[32];
static void *heap_registry[32];
static int64_t fixture_clock;
static bool lock_storage(void) { return true; }
static void unlock_storage(void) {}
static int64_t esp_timer_get_time(void) { return ++fixture_clock; }
static void content_validation_note_bytes(size_t bytes) {(void)bytes;}
static void arena_sprite_end_locked(void) {}
static void heap_caps_get_info(multi_heap_info_t *info,unsigned caps) {
    (void)caps;++heap_observations;*info=(multi_heap_info_t){0};
}
static size_t heap_caps_get_total_size(unsigned caps) {(void)caps;++heap_observations;return 0;}
static void *fixture_alloc(size_t size) {
    if(++allocation_attempts==fail_allocation)return NULL;
    void *p=malloc(size);assert(p);
    unsigned i=0;while(i<32&&heap_registry[i])++i;assert(i<32);
    heap_registry[i]=p;++allocations;++live_allocations;return p;
}
static void *heap_caps_malloc(size_t size,unsigned caps) {(void)caps;return fixture_alloc(size);}
static void *heap_caps_aligned_alloc(size_t align,size_t size,unsigned caps) {
    (void)align;(void)caps;if(fail_batch)return NULL;return fixture_alloc(size);
}
static void heap_caps_free(void *pointer) {
    if(!pointer)return;
    unsigned i=0;while(i<32&&heap_registry[i]!=pointer)++i;
    /* Reject free of borrowed ROM, double-free, or any unowned pointer. */
    assert(i<32&&live_allocations);heap_registry[i]=NULL;--live_allocations;++frees;free(pointer);
}
static FILE *fixture_fopen(const char *path,const char *mode) {
    unsigned i=0;while(i<P4_GCA_FILE_COUNT&&strcmp(path,s_arena_content[i].path))++i;
    assert(i<P4_GCA_FILE_COUNT);
    const char *actual=override_path&&i==override_index?override_path:local_paths[i];
    FILE *f=fopen(actual,mode);if(!f)return NULL;
    unsigned slot=0;while(slot<32&&file_registry[slot])++slot;assert(slot<32);
    file_registry[slot]=f;++file_opens;++live_files;return f;
}
static int fixture_fclose(FILE *file) {
    unsigned i=0;while(i<32&&file_registry[i]!=file)++i;assert(i<32&&live_files);
    file_registry[i]=NULL;--live_files;++file_closes;return fclose(file);
}
typedef p4_sha256_t mbedtls_sha256_context;
static void mbedtls_sha256_init(mbedtls_sha256_context *s) {p4_sha256_init(s);}
static int mbedtls_sha256_starts(mbedtls_sha256_context *s,int kind) {assert(kind==0);p4_sha256_init(s);return 0;}
static int mbedtls_sha256_update(mbedtls_sha256_context *s,const void *data,size_t size) {p4_sha256_update(s,data,size);return 0;}
static int mbedtls_sha256_finish(mbedtls_sha256_context *s,uint8_t *out) {p4_sha256_finish(s,out);return 0;}
static void mbedtls_sha256_free(mbedtls_sha256_context *s) {(void)s;}
static int mbedtls_sha256(const void *data,size_t size,uint8_t *out,int kind) {
    mbedtls_sha256_context s;mbedtls_sha256_starts(&s,kind);mbedtls_sha256_update(&s,data,size);return mbedtls_sha256_finish(&s,out);
}
static game_storage_content_t inspect_exact_file(const char *path,uint64_t size,const uint8_t *hash,
    bool wad,bool chex,esp_err_t *error,uint8_t **data,platform_game_storage_progress_fn_t progress,void *context) {
    (void)path;(void)size;(void)hash;(void)wad;(void)chex;(void)error;(void)data;(void)progress;(void)context;
    assert(0 && "Non-Arena branch is outside this fixture");return GAME_STORAGE_CONTENT_INVALID;
}
#define fopen fixture_fopen
#define fclose fixture_fclose
'''
CASES=r'''
static void progress(void *context) {
    assert(context==&callbacks);++callbacks;
    platform_game_storage_doom_load_progress_t p;
    assert(platform_game_storage_get_doom_load_progress(&p)==ESP_OK);
    assert(p.bytes_checked<=p.bytes_total);
    if(p.checking_trusted_metadata){++metadata_callbacks;assert(p.active&&!p.complete&&!p.checking_structure);}
    if(p.checking_structure){++structure_callbacks;assert(p.active&&!p.complete&&!p.checking_trusted_metadata);}
    if(p.complete)assert(!p.active&&!p.checking_structure&&!p.checking_trusted_metadata&&p.bytes_checked==p.bytes_total);
}
int main(int argc,char **argv) {
    assert(argc==2||argc==4);const char *test=argv[1];
    assert(!P4_ARENA_BASE_PSRAM_ENABLED);
    if(argc==4){override_index=(unsigned)strtoul(argv[2],NULL,10);override_path=argv[3];}
    s_model=(game_storage_model_t){.owner=GAME_STORAGE_OWNER_APP,.content=GAME_STORAGE_CONTENT_READY};
    const platform_game_storage_doom_title_t title=PLATFORM_GAME_STORAGE_DOOM_TITLE_GAME_CHANGERS_AI;
    assert(platform_game_storage_lock_for_doom_title_with_policy((platform_game_storage_doom_title_t)-1,
        PLATFORM_GAME_STORAGE_VERIFY_FULL_CONTENT,NULL,NULL)==ESP_ERR_INVALID_ARG);
    assert(platform_game_storage_lock_for_doom_title_with_policy(title,(platform_game_storage_verification_t)7,NULL,NULL)==ESP_ERR_INVALID_ARG);
    assert(platform_game_storage_lock_for_doom_title_with_policy(PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM,
        PLATFORM_GAME_STORAGE_VERIFY_TRUSTED_ON_READ,NULL,NULL)==ESP_ERR_INVALID_ARG);
    assert(platform_game_storage_lock_for_doom_title_with_policy(PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST,
        PLATFORM_GAME_STORAGE_VERIFY_TRUSTED_ON_READ,NULL,NULL)==ESP_ERR_INVALID_ARG);
    assert(file_opens==0&&allocations==0&&!s_model.launch_pending);
#if !CONFIG_P4_BOARD_M5STACK_TAB5
    assert(!strcmp(test,"legacy-policy"));
    assert(platform_game_storage_lock_for_doom_title_with_policy(title,PLATFORM_GAME_STORAGE_VERIFY_TRUSTED_ON_READ,NULL,NULL)==ESP_ERR_INVALID_ARG);
    assert(file_opens==0&&allocations==0&&heap_observations==0);return 0;
#else
    bool full=!strcmp(test,"full")||!strcmp(test,"corrupt-full")||!strcmp(test,"allocation-fail")||!strcmp(test,"batch-fallback");
    if(!strcmp(test,"allocation-fail"))fail_allocation=1;
    if(!strcmp(test,"batch-fallback"))fail_batch=true;
    esp_err_t result;
    if(full)result=platform_game_storage_lock_for_doom_title_with_progress(title,progress,&callbacks);
    else result=platform_game_storage_lock_for_doom_title_with_policy(title,PLATFORM_GAME_STORAGE_VERIFY_TRUSTED_ON_READ,progress,&callbacks);
    bool success=!strcmp(test,"full")||!strcmp(test,"lazy")||!strcmp(test,"corrupt-lazy")||!strcmp(test,"batch-fallback");
    if(success){
        assert(result==ESP_OK&&s_model.owner==GAME_STORAGE_OWNER_GAME&&!s_model.launch_pending);
        assert(live_files==3&&s_doom_load_progress.complete&&structure_callbacks>=3);
        assert(s_doom_load_progress.bytes_total==(full?UINT64_C(16775668):UINT64_C(99418)));
        assert(s_locked_wad_size_bytes == (size_t)s_arena_content[P4_GCA_BASE].bytes);
        assert(metadata_callbacks==(full?0U:3U));
        for(unsigned i=0;i<3;++i){
            assert(s_arena_file[i]);
            if(full)assert(s_arena_hashes[i]&&s_arena_reader[i].digests==s_arena_hashes[i]);
            else assert(!s_arena_hashes[i]&&s_arena_reader[i].digests==p4_arena_trusted_wad_tables[i].digests);
        }
        if(!strcmp(test,"corrupt-lazy")){
            uint8_t out[32];memset(out,0xa5,sizeof(out));
            assert(!p4_verified_read(&s_arena_reader[0],4096,out,sizeof(out)));
            for(unsigned i=0;i<sizeof(out);++i)assert(out[i]==0xa5);
            assert(s_arena_reader[0].failed);
            assert(!p4_verified_read(&s_arena_reader[0],0,out,sizeof(out)));
        }else{
            uint8_t out[16];assert(p4_verified_read(&s_arena_reader[0],4096,out,sizeof(out)));
        }
    }else{
        assert(result!=ESP_OK&&s_model.owner==GAME_STORAGE_OWNER_APP&&!s_model.launch_pending);
        assert(!s_doom_load_progress.complete&&!s_doom_load_progress.active);
        assert(live_files==0&&live_allocations==0);
        if(!strcmp(test,"table-mismatch"))assert(file_opens==0);
        if(!strcmp(test,"missing-pwad"))assert(file_opens==1&&file_closes==1);
        if(!strcmp(test,"bad-support"))assert(file_opens==4&&file_closes==4);
    }
    release_locked_doom_snapshot();release_locked_doom_snapshot();
    assert(!heap_observations&&!s_arena_base_pending&&!s_arena_base_resident&&!s_arena_base_resident_bytes);
    assert(live_files==0&&live_allocations==0&&file_opens==file_closes&&allocations==frees);
    for(unsigned i=0;i<3;++i)assert(!s_arena_file[i]&&!s_arena_hashes[i]&&!s_arena_reader[i].digests);
    printf("PASS %s opens=%u allocations=%u checked=%" PRIu64 " total=%" PRIu64 "\n",test,file_opens,allocations,s_doom_load_progress.bytes_checked,s_doom_load_progress.bytes_total);
    return 0;
#endif
}
'''
class RuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        output=os.environ.get('P4_TRUSTED_RUNTIME_OUT')
        if output:
            cls.work=Path(output).resolve();cls.work.mkdir(parents=True,exist_ok=True)
            if any(cls.work.iterdir()):raise AssertionError('Evidence output must be empty')
        else:
            cls.temp=tempfile.TemporaryDirectory(prefix='p4-trusted-runtime-');cls.addClassCleanup(cls.temp.cleanup);cls.work=Path(cls.temp.name)
        cls.results=[]
        cls.files=json.loads((INPUT_ROOT/'third_party/game-data.json').read_text())['game_changers_ai_bundle']['files']
        source=(ROOT/'components/platform_game_storage/src/platform_game_storage.c').read_text()
        names=('release_locked_doom_snapshot','arena_read_block','arena_digest','arena_psram_observe','arena_psram_has_reserve','arena_base_snapshot_begin','arena_base_snapshot_recheck','arena_read_memory','inspect_arena_structure','inspect_arena_stream','inspect_arena_trusted','inspect_doom_title_snapshot','platform_game_storage_get_doom_load_progress','platform_game_storage_lock_for_doom_title_with_policy','platform_game_storage_lock_for_doom_title_with_progress')
        units=[('#if CONFIG_P4_BOARD_M5STACK_TAB5\n'+EXTRACT.function(source,n)+'\n#endif\n') if n=='inspect_arena_trusted' else EXTRACT.function(source,n) for n in names]
        gates=source[source.index('#ifndef P4_ARENA_BASE_PSRAM'):source.index('#define P4_GAME_STORAGE_SD_BACKEND')]
        paths='static const char *const local_paths[]={'+','.join(json.dumps(str(INPUT_ROOT/f['local_path'])) for f in cls.files)+'};\n'
        (cls.work/'runtime.c').write_text(paths+FIXTURE+gates+'\n'.join(units)+CASES)
        errors=('ESP_ERR_INVALID_ARG','ESP_ERR_INVALID_STATE','ESP_ERR_NOT_FOUND','ESP_ERR_INVALID_SIZE','ESP_ERR_NO_MEM','ESP_ERR_INVALID_CRC','ESP_ERR_INVALID_RESPONSE')
        (cls.work/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n'+'\n'.join(f'#define {n} {i+1}' for i,n in enumerate(errors)))
        GEN.generate(INPUT_ROOT,cls.work/'generated');generated=cls.work/'generated/arena_trusted_digests.c'
        mutant=cls.work/'mismatched-table.c';text=generated.read_text();first=text.index('0x');text=text[:first]+'0x'+f'{int(text[first+2:first+4],16)^1:02x}'+text[first+4:];mutant.write_text(text)
        src=ROOT/'components/platform_game_storage/src';sha=ROOT/'components/p4_usb_content_transfer/tests/stubs'
        cls.commands=[]
        for name,tab5,table in (('runtime',1,generated),('legacy',0,generated),('mismatch',1,mutant)):
            cmd=[os.environ.get('CC','cc'),'-std=c11','-D_POSIX_C_SOURCE=200809L',f'-DCONFIG_P4_BOARD_M5STACK_TAB5={tab5}','-Wall','-Wextra','-Werror','-Wconversion','-Wno-unused-function','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cls.work),'-I',str(ROOT/'components/platform_game_storage/include'),'-I',str(src),'-I',str(sha),str(cls.work/'runtime.c'),str(table),*[str(src/n) for n in ('trusted_wad_table.c','verified_reader.c','verified_wad.c','storage_block_read.c','game_storage_model.c')],str(sha/'sha256.c'),'-o',str(cls.work/name)]
            result=subprocess.run(cmd,capture_output=True,text=True,timeout=30);cls.commands.append(cmd)
            (cls.work/f'compile-{name}.log').write_text(result.stdout+result.stderr)
            (cls.work/'compile-commands.json').write_text(json.dumps(cls.commands,indent=2)+'\n')
            if result.returncode:raise AssertionError(result.stdout+result.stderr)
    def run_case(self,name,index=None,path=None,exe='runtime'):
        args=[str(self.work/exe),name]
        if index is not None:args.extend((str(index),str(path)))
        result=subprocess.run(args,capture_output=True,text=True,timeout=30)
        self.results.append({'case':name,'command':args,'returncode':result.returncode,'stdout':result.stdout,'stderr':result.stderr})
        (self.work/'case-results.json').write_text(json.dumps(self.results,indent=2)+'\n')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    def altered(self,index,kind):
        p=self.work/f'{index}-{kind}';shutil.copyfile(INPUT_ROOT/self.files[index]['local_path'],p)
        with p.open('r+b') as f:
            if kind=='truncate':f.truncate(p.stat().st_size-1)
            elif kind=='extend':f.seek(0,2);f.write(b'x')
            else:f.seek(4096 if index==0 else 0);value=f.read(1);f.seek(-1,1);f.write(bytes([value[0]^1]))
        self.addCleanup(p.unlink,missing_ok=True);return p
    def test_full_public_wrapper_scans_entire_exact_bundle(self):self.run_case('full')
    def test_lazy_uses_rom_tables_and_verifies_support_files(self):self.run_case('lazy')
    def test_unconsumed_corruption_is_rejected_before_engine_copy(self):self.run_case('corrupt-lazy',0,self.altered(0,'corrupt'))
    def test_same_corruption_is_rejected_by_explicit_full_scan(self):self.run_case('corrupt-full',0,self.altered(0,'corrupt'))
    def test_truncated_descriptor_rejected(self):self.run_case('truncated',0,self.altered(0,'truncate'))
    def test_extended_descriptor_rejected(self):self.run_case('extended',0,self.altered(0,'extend'))
    def test_missing_second_wad_releases_borrowed_first(self):self.run_case('missing-pwad',1,self.work/'missing.wad')
    def test_wrong_support_sha_releases_all_borrowed_wads(self):self.run_case('bad-support',3,self.altered(3,'corrupt'))
    def test_owned_hash_allocation_failure_closes_file(self):self.run_case('allocation-fail')
    def test_batch_allocation_failure_preserves_full_verification(self):self.run_case('batch-fallback')
    def test_mismatched_compiled_table_fails_before_open(self):self.run_case('table-mismatch',exe='mismatch')
    def test_legacy_policy_rejects_lazy_without_resource_changes(self):self.run_case('legacy-policy',exe='legacy')
if __name__=='__main__':unittest.main()
