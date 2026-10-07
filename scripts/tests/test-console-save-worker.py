#!/usr/bin/env python3
"""Run extracted Console OS save worker against real save service/store.

Mocks only the RTOS allocator/scheduler and NVS seal boundary. The flash stub
rejects external worker stacks before a flash read, as pinned IDF does. Set
P4_CONSOLE_SAVE_SOURCE to exercise an immutable source baseline/candidate.
"""
import os
import re
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ.get("P4_CONSOLE_REPO_ROOT", Path(__file__).resolve().parents[2]))
SOURCE = Path(os.environ.get("P4_CONSOLE_SAVE_SOURCE", ROOT / "apps/console_os/main/console_os_main.c"))


def function(source, name):
    marker = source.index(name + "(")
    start = source.rfind("\nstatic ", 0, marker) + 1
    brace = source.index("{", marker)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "p4/game_save_service.h"

typedef int BaseType_t;
typedef struct { bool ready; bool binary; } StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
typedef int esp_err_t;
typedef struct { const char *id; } p4_game_package_info_t;
enum { pdPASS=1, pdTRUE=1, pdFALSE=0, ESP_OK=0,
       MALLOC_CAP_INTERNAL=1, MALLOC_CAP_SPIRAM=2, MALLOC_CAP_8BIT=4,
       tskIDLE_PRIORITY=0, eSetBits=1, P4_PROTECTED_GAME_REJECTED=1,
       PLATFORM_SAVE_SEAL_KEY_BYTES=32,
       CONSOLE_GAME_SAVE_STOP_TIMEOUT_MS=5000 };
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(value) (value)
#define TAG "test"
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGI(...) test_log(__VA_ARGS__)
static void test_log(const char *tag,const char *format,...) {(void)tag;(void)format;}
static const char *esp_err_to_name(esp_err_t e) {(void)e;return "mock";}
static char *test_root;
#define PLATFORM_GAME_STORAGE_MOUNT_POINT test_root
static bool fail_create, worker_context, used_caps, task_deleted;
static unsigned stack_caps, allocations, flash_reads, flash_writes;
static unsigned create_calls, create_stack_bytes, notify_bits;
static TaskFunction_t task_entry;
static void *task_argument;
static uint32_t seal_sequence, catalog_sequence;
static size_t catalog_bytes;
static uint8_t seal_sha[32];
static bool seal_closed;
static int protected_game_lineage_check(const p4_game_package_info_t *p) {(void)p;return 0;}
static void *heap_caps_calloc(size_t n,size_t s,unsigned caps) {assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));void *p=calloc(n,s);if(p)++allocations;return p;}
static void *heap_caps_malloc(size_t n,unsigned caps) {assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));void *p=malloc(n);if(p)++allocations;return p;}
static void heap_caps_free(void *p) {if(p){assert(allocations);--allocations;free(p);}}
static SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *p) {p->ready=true;return p;}
static SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *p) {p->binary=true;return p;}
static BaseType_t xSemaphoreGive(SemaphoreHandle_t p) {p->ready=true;return pdTRUE;}
static void run_worker(void) {assert(task_entry && !task_deleted);worker_context=true;task_entry(task_argument);worker_context=false;assert(task_deleted);}
static BaseType_t xSemaphoreTake(SemaphoreHandle_t p,uint32_t timeout) {(void)timeout;if(p->binary&&!p->ready&&notify_bits)run_worker();if(!p->ready)return pdFALSE;p->ready=false;return pdTRUE;}
static BaseType_t create_task(TaskFunction_t entry,const char *name,unsigned bytes,void *arg,unsigned priority,TaskHandle_t *handle,unsigned caps,bool with_caps) {
 assert(!strcmp(name,"game_save"));assert(priority==1);++create_calls;create_stack_bytes=bytes;stack_caps=caps;used_caps=with_caps;
 if(fail_create)return pdFALSE;task_entry=entry;task_argument=arg;task_deleted=false;*handle=arg;return pdPASS;
}
static BaseType_t xTaskCreate(TaskFunction_t f,const char *n,unsigned b,void *a,unsigned p,TaskHandle_t *h) {return create_task(f,n,b,a,p,h,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT,false);}
static BaseType_t xTaskCreateWithCaps(TaskFunction_t f,const char *n,unsigned b,void *a,unsigned p,TaskHandle_t *h,unsigned c) {return create_task(f,n,b,a,p,h,c,true);}
static BaseType_t xTaskNotify(TaskHandle_t h,uint32_t bits,int action) {assert(h==task_argument);assert(action==eSetBits);notify_bits|=bits;return pdPASS;}
static BaseType_t xTaskNotifyWait(uint32_t clear,uint32_t exit_clear,uint32_t *bits,uint32_t timeout) {(void)clear;(void)exit_clear;(void)timeout;assert(notify_bits);*bits=notify_bits;notify_bits=0;return pdTRUE;}
static unsigned uxTaskGetStackHighWaterMark(TaskHandle_t h) {assert(!h);return 2048;}
static void vTaskDelete(TaskHandle_t h) {assert(!h);assert(!used_caps && "WithCaps requires matching deletion API");task_deleted=true;}
static void flash_guard(bool write) {
 if(worker_context){assert((stack_caps&MALLOC_CAP_INTERNAL)!=0 && (stack_caps&MALLOC_CAP_SPIRAM)==0 && "NVS requires internal task stack");if(write)++flash_writes;else ++flash_reads;}
}
static esp_err_t platform_save_seal_load_key(uint8_t *key) {flash_guard(false);memset(key,0x37,32);return ESP_OK;}
static esp_err_t platform_save_seal_legacy_is_closed(const char *g,const char *s,bool *closed) {(void)g;(void)s;flash_guard(false);*closed=seal_closed;return ESP_OK;}
static esp_err_t platform_save_seal_close_legacy(const char *g,const char *s) {(void)g;(void)s;flash_guard(true);seal_closed=true;return ESP_OK;}
static esp_err_t platform_save_seal_object_is_allowed(const char *g,const char *s,uint32_t seq,const uint8_t *sha,bool *allowed) {(void)g;(void)s;flash_guard(false);*allowed=seq>=seal_sequence&&(seq!=seal_sequence||!memcmp(sha,seal_sha,32));return ESP_OK;}
static esp_err_t platform_save_seal_advance_object(const char *g,const char *s,uint32_t seq,const uint8_t *sha) {(void)g;(void)s;flash_guard(true);assert(seq>=seal_sequence);seal_sequence=seq;memcpy(seal_sha,sha,32);return ESP_OK;}
static esp_err_t platform_save_seal_object_sequence(const char *g,const char *s,uint32_t *seq) {(void)g;(void)s;flash_guard(false);*seq=seal_sequence;return ESP_OK;}
static p4_game_save_storage_mode_t cartridge_save_storage_mode(void) {return P4_GAME_SAVE_STORAGE_WRITABLE;}
static void save_catalog_record(const char *g,const char *s,size_t b,uint32_t seq) {assert(!strcmp(g,"org.p4console.wacky-probe"));assert(!strcmp(s,"AUTO"));catalog_bytes=b;catalog_sequence=seq;}
'''

MAIN = r'''
int main(int argc,char **argv) {
 assert(argc==3);test_root=argv[2];
 p4_game_package_info_t package={"org.p4console.wacky-probe"};
 fail_create=!strcmp(argv[1],"allocation-failure");
 cartridge_save_runtime_t *runtime=cartridge_save_open(&package);
 assert(runtime&&create_calls==1&&create_stack_bytes==12288);
 assert(!used_caps&&stack_caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
 uint8_t payload[36]={'W','W','S',2,1,2,3,0,0,1,1,1};
 p4_game_save_ticket_t ticket=0;
 if(fail_create){
  assert(!runtime->worker&&!p4_game_save_service_available(&runtime->service));
  assert(!p4_game_save_service_queue(&runtime->service,"AUTO",2,0,payload,sizeof(payload),&ticket));
  assert(!ticket&&cartridge_save_close(runtime,package.id)&&allocations==0);
  assert(!flash_reads&&!flash_writes);puts("PASS allocation failure withholds save capability");return 0;
 }
 assert(p4_game_save_service_queue(&runtime->service,"AUTO",2,0,payload,sizeof(payload),&ticket));
 assert(ticket);
 if(!strcmp(argv[1],"work")){
  notify_bits=CARTRIDGE_SAVE_NOTIFY_WORK|CARTRIDGE_SAVE_NOTIFY_STOP;run_worker();
  p4_game_save_status_t status;uint32_t sequence;
  assert(p4_game_save_service_read_status(&runtime->service,ticket,&status,&sequence));
  assert(status==P4_GAME_SAVE_COMMITTED&&sequence==1&&runtime->processed_requests==1);
  runtime->worker=NULL;
 }
 assert(cartridge_save_close(runtime,package.id));
 assert(task_deleted&&flash_reads&&flash_writes&&catalog_sequence==1&&catalog_bytes==36&&allocations==0);
 /* Reopen the exact authenticated payload after worker teardown. */
 runtime=cartridge_save_open(&package);
 assert(runtime&&runtime->service.launch_sequence==1&&runtime->service.launch_schema_version==2);
 assert(runtime->service.launch_snapshot_bytes==36&&!memcmp(runtime->launch_snapshot,payload,36));
 if(!strcmp(argv[1],"repeat-exit")){
  /* Match the device failure: reopen sequence 1, then update and drain on exit.
   * Continue through independent workers, checking each authenticated reopen. */
  for(uint32_t expected=1;expected<5;++expected){
   payload[12]=(uint8_t)(expected*17U);
   ticket=0;
   assert(p4_game_save_service_queue(&runtime->service,"AUTO",2,expected,payload,sizeof(payload),&ticket));
   assert(ticket&&cartridge_save_close(runtime,package.id));
   assert(task_deleted&&allocations==0&&catalog_sequence==expected+1&&catalog_bytes==36);
   runtime=cartridge_save_open(&package);
   assert(runtime&&create_stack_bytes==12288&&!used_caps);
   assert(runtime->service.launch_sequence==expected+1&&runtime->service.launch_schema_version==2);
   assert(runtime->service.launch_snapshot_bytes==36&&!memcmp(runtime->launch_snapshot,payload,36));
  }
 }
 assert(cartridge_save_close(runtime,package.id)&&allocations==0);
 puts("PASS flash-safe commit, stop drain, release and authenticated reopen");return 0;
}
'''


class SaveWorker(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="p4-save-worker-")
        cls.directory = Path(cls.temp.name)
        text = SOURCE.read_text()
        # Use the production declaration: this host test checks admission bytes,
        # not the actual ESP32 task stack's worst-case consumption.
        constants = re.findall(r"^    CONSOLE_GAME_SAVE_STACK_BYTES = [0-9]+ \* 1024,$", text, re.M)
        if len(constants) != 1:
            raise AssertionError("missing or ambiguous production save stack size")
        stack_enum = "enum { " + constants[0].strip().rstrip(",") + " };\n"
        start = text.index("enum {\n    CARTRIDGE_SAVE_NOTIFY_WORK")
        end = text.index("} cartridge_save_runtime_t;", start) + len("} cartridge_save_runtime_t;")
        names = ("cartridge_save_legacy_query", "cartridge_save_legacy_close",
                 "cartridge_save_object_query", "cartridge_save_object_advance",
                 "cartridge_save_object_sequence", "cartridge_save_worker",
                 "cartridge_save_release_allocations", "cartridge_save_open", "cartridge_save_close")
        generated = cls.directory / "save_worker.c"
        generated.write_text(PREFIX + stack_enum + text[start:end] + "\n" + "\n".join(function(text,n) for n in names) + MAIN)
        cls.binary = cls.directory / "save_worker"
        command = [os.environ.get("CC", "cc"), "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g", "-I"+str(ROOT/"components/p4_game_save/include"), "-I"+str(ROOT/"components/p4_game_api/include"), str(generated)]
        command += [str(ROOT/"components/p4_game_save/src"/name) for name in ("game_save.c","memory.c","service.c","sha256.c","store.c")]
        subprocess.run(command+["-o",str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def case(self, name):
        directory = self.directory / name
        directory.mkdir()
        subprocess.run([str(self.binary),name,str(directory)],check=True)

    def test_save_work_commits_and_reopens(self): self.case("work")
    def test_exit_stop_drains_pending_save(self): self.case("exit")
    def test_repeated_exit_save_reopens_exact_new_sequence(self): self.case("repeat-exit")
    def test_no_internal_stack_withholds_capability(self): self.case("allocation-failure")


if __name__ == "__main__":
    unittest.main()
