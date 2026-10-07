// SPDX-License-Identifier: MIT
/* Run the actual byte parser and filesystem transfer code with only SDK services
 * replaced. The SHA-256 adapter uses the repository's real portable hash. */
#include "fake_sdk.h"
#include "p4/content_transfer.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int64_t now_us;
static uint8_t response[1024];
static uint32_t active_baud;
static bool fail_idle_baud;
static size_t response_bytes;
static unsigned baud_changes;
void *heap_caps_malloc(size_t bytes,unsigned caps){(void)caps;return malloc(bytes);}
void heap_caps_free(void *pointer){free(pointer);}
int64_t esp_timer_get_time(void){return now_us;}
int esp_task_wdt_reset(void){return 0;}
void vTaskDelay(uint32_t ticks){now_us+=(int64_t)ticks*1000;}
void transfer_test_log(const char *tag,const char *format,...){(void)tag;(void)format;}
void mbedtls_sha256_init(mbedtls_sha256_context *c){p4_sha256_init(c);}
void mbedtls_sha256_free(mbedtls_sha256_context *c){memset(c,0,sizeof(*c));}
int mbedtls_sha256_starts(mbedtls_sha256_context *c,int mode){assert(mode==0);p4_sha256_init(c);return 0;}
int mbedtls_sha256_update(mbedtls_sha256_context *c,const unsigned char *b,size_t n){p4_sha256_update(c,b,n);return 0;}
int mbedtls_sha256_finish(mbedtls_sha256_context *c,unsigned char out[32]){p4_sha256_finish(c,out);return 0;}
static esp_err_t send_bytes(void *context,const uint8_t *b,size_t n){(void)context;assert(response_bytes+n<sizeof(response));memcpy(response+response_bytes,b,n);response_bytes+=n;response[response_bytes]=0;return ESP_OK;}
static esp_err_t wait_tx(void *context,uint32_t ms){(void)context;(void)ms;return ESP_OK;}
static esp_err_t set_baud(void *context,uint32_t baud){(void)context;++baud_changes;if(fail_idle_baud && baud==115200)return ESP_ERR_INVALID_STATE;active_baud=baud;return ESP_OK;}
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(8*i));}
static uint32_t crc(const uint8_t *p,size_t n){uint32_t c=UINT32_MAX;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?UINT32_C(0xedb88320):0);}return ~c;}

static void manifest(uint8_t kind,const uint8_t *data,size_t length) {
 uint8_t m[48]={'P','4','M','1',kind};put32(m+8,(uint32_t)length);
 p4_sha256_t sha;p4_sha256_init(&sha);p4_sha256_update(&sha,data,length);p4_sha256_finish(&sha,m+12);put32(m+44,crc(m,44));
 response_bytes=0;for(size_t i=0;i<48;i+=8)(void)p4_content_transfer_consume(m+i,8);
 assert(response_bytes>=11 && !memcmp(response,"P4R1",4));
}
static void payload(const uint8_t *data,size_t length,bool bad_crc) {
 uint8_t c[4112]={'P','4','C','1'};assert(length<=4096);c[8]=(uint8_t)length;c[9]=(uint8_t)(length>>8);
 put32(c+12,crc(data,length)^(bad_crc?1U:0U));memcpy(c+16,data,length);response_bytes=0;
 assert(p4_content_transfer_consume(c,16+length));
}
static void resume(void) {
 assert(p4_content_transfer_info().busy);response_bytes=0;now_us+=400000;p4_content_transfer_poll();
 assert(p4_content_transfer_info().state==P4_CONTENT_TRANSFER_IDLE);
 assert(!p4_content_transfer_info().busy && active_baud==115200);
 assert(response_bytes && strstr((char *)response,"reboot=0"));
}
static size_t load(const char *name,uint8_t *out) {
 char path[1024];snprintf(path,sizeof(path),"%s/%s",PACK_ROOT,name);FILE *f=fopen(path,"rb");assert(f);
 size_t n=fread(out,1,4096,f);assert(n>0 && n<4096 && feof(f));assert(fclose(f)==0);return n;
}
int main(void) {
 char root[]="/tmp/p4-content-batch-XXXXXX";assert(mkdtemp(root));
 const p4_content_transfer_transport_t t={send_bytes,wait_tx,set_baud,NULL,115200};assert(p4_content_transfer_init(root,&t)==ESP_OK);p4_content_transfer_set_available(true);
 uint8_t data[4096];size_t n=load("licenses/MIT.txt",data);manifest(20,data,n);assert(response[4]==0 && active_baud==921600);payload(data,n,false);
 assert(p4_content_transfer_info().state==P4_CONTENT_TRANSFER_INSTALLED && p4_content_transfer_info().generation==1);
 /* Failure to restore the idle transport must keep the service exclusive. */
 fail_idle_baud=true;now_us+=400000;p4_content_transfer_poll();assert(p4_content_transfer_info().busy);fail_idle_baud=false;resume();
 n=load("licenses/Freedoom-0.13.0-COPYING.txt",data);manifest(7,data,n);assert(response[4]==0);payload(data,n,false);assert(p4_content_transfer_info().generation==2);resume();
 /* Existing-file proof needs no restart or baud switch. */
 unsigned before=baud_changes;manifest(7,data,n);assert(response[4]==10 && baud_changes==before && !p4_content_transfer_info().busy);
 n=load("LICENSE",data);manifest(16,data,n);assert(response[4]==0);payload(data,n,true);assert(p4_content_transfer_info().state==P4_CONTENT_TRANSFER_FAILED && p4_content_transfer_info().generation==2);resume();
 manifest(16,data,n);assert(response[4]==0);payload(data,n,false);assert(p4_content_transfer_info().generation==3);resume();
 n=load("CREDITS.md",data);manifest(17,data,n);assert(response[4]==0);now_us+=16000000;p4_content_transfer_poll();assert(p4_content_transfer_info().last_status==8);resume();
 manifest(17,data,n);assert(response[4]==0);payload(data,n,false);assert(p4_content_transfer_info().generation==4);resume();
 /* The real service links without esp_restart and accepts the next file after
  * success, duplicate, CRC failure and a disconnected-sender timeout. */
 char path[512];const char *files[]={"GCADOOM/licenses/MIT.txt","GCADOOM/licenses/Freedoom-0.13.0-COPYING.txt","GCADOOM/LICENSE","GCADOOM/CREDITS.md"};
 for(size_t i=0;i<4;++i){snprintf(path,sizeof(path),"%s/%s",root,files[i]);assert(unlink(path)==0);}
 const char *dirs[]={"GCADOOM/licenses","GCADOOM/music","GCADOOM"};for(size_t i=0;i<3;++i){snprintf(path,sizeof(path),"%s/%s",root,dirs[i]);assert(rmdir(path)==0);}assert(rmdir(root)==0);
 puts("content batch: multiple verified files, duplicate, CRC retry and timeout recovery without reboot");return 0;
}
