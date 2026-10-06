// SPDX-License-Identifier: MIT
/* Run the actual byte parser and filesystem transfer code with only SDK services
 * replaced. The SHA-256 adapter uses the repository's real portable hash. */
#include "fake_sdk.h"
#include "p4/file_transfer.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int64_t now_us;
static uint8_t response[256];
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
static esp_err_t send_bytes(void *context,const uint8_t *b,size_t n){(void)context;assert(response_bytes+n<=sizeof(response));memcpy(response+response_bytes,b,n);response_bytes+=n;return ESP_OK;}
static esp_err_t wait_tx(void *context,uint32_t ms){(void)context;(void)ms;return ESP_OK;}
static esp_err_t set_baud(void *context,uint32_t baud){(void)context;(void)baud;++baud_changes;return ESP_OK;}
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(8*i));}
static uint32_t crc(const uint8_t *p,size_t n){uint32_t c=UINT32_MAX;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?UINT32_C(0xedb88320):0);}return ~c;}
static void request(uint8_t direction,uint8_t kind,const char *name,size_t split,const uint8_t *digest,uint32_t size){
 uint8_t r[88]={ 'P','4','F','1',direction,kind };put32(r+8,size);if(digest)memcpy(r+12,digest,32);assert(strlen(name)<40);memcpy(r+44,name,strlen(name));put32(r+84,crc(r,84));
 response_bytes=0;for(size_t i=0;i<sizeof(r);){size_t n=sizeof(r)-i;if(n>split)n=split;bool claimed=p4_file_transfer_consume(r+i,n);i+=n;if(i==sizeof(r))assert(claimed);}
}
static void reset_terminal(void){now_us+=UINT64_C(1100000);p4_file_transfer_poll();assert(p4_file_transfer_info().state==P4_FILE_TRANSFER_IDLE);}
int main(void){
 char root[]="/tmp/p4-native-transfer-XXXXXX";assert(mkdtemp(root));
 const p4_content_transfer_transport_t t={send_bytes,wait_tx,set_baud,NULL,115200};assert(p4_file_transfer_init(root,&t)==ESP_OK);
 char path[512];snprintf(path,sizeof(path),"%s/P4",root);assert(mkdir(path,0700)==0);snprintf(path,sizeof(path),"%s/P4/GAMES",root);assert(mkdir(path,0700)==0);snprintf(path,sizeof(path),"%s/P4/GAMES/OLD.P4CART",root);FILE *f=fopen(path,"wb");assert(f);assert(fwrite("archive",1,7,f)==7);assert(fclose(f)==0);
 const size_t splits[]={1,7,88};p4_file_transfer_set_available(true);
 for(uint8_t direction=1;direction<=3;++direction)for(size_t i=0;i<3;++i){
  request(direction,4,"OLD.P4CART",splits[i],NULL,0);assert(response_bytes==52);assert(memcmp(response,"P4R2",4)==0);assert(response[4]==P4_FILE_TRANSFER_STATUS_UNSUPPORTED);assert(baud_changes==0);assert(p4_file_transfer_info().generation==0);assert(!p4_file_transfer_info().busy);reset_terminal();
 }
 f=fopen(path,"rb");assert(f);char old[8]={0};assert(fread(old,1,8,f)==7);assert(fclose(f)==0);assert(strcmp(old,"archive")==0);
 p4_file_transfer_set_available(false);
 for(uint8_t kind=1;kind<=3;++kind){request(1,kind,kind==1?"OK.P4G":kind==3?"OK.P4R":"NOTE.TXT",7,NULL,1);assert(response[4]==P4_FILE_TRANSFER_STATUS_STORAGE);reset_terminal();}
 /* A successful opaque exchange still exercises streaming, real hash checking,
  * atomic activation and generation publication after rejecting legacy carts. */
 p4_file_transfer_set_available(true);const uint8_t payload[]={3,1,4,1,5};uint8_t digest[32];p4_sha256_t sha;p4_sha256_init(&sha);p4_sha256_update(&sha,payload,sizeof(payload));p4_sha256_finish(&sha,digest);
 request(1,2,"NOTE.TXT",88,digest,sizeof(payload));assert(response[4]==P4_FILE_TRANSFER_STATUS_OK);assert(p4_file_transfer_info().busy);
 uint8_t chunk[21]={'P','4','C','2'};chunk[8]=sizeof(payload);put32(chunk+12,crc(payload,sizeof(payload)));memcpy(chunk+16,payload,sizeof(payload));response_bytes=0;assert(p4_file_transfer_consume(chunk,sizeof(chunk)));assert(p4_file_transfer_info().state==P4_FILE_TRANSFER_COMPLETE);assert(p4_file_transfer_info().generation==1);assert(p4_file_transfer_info().last_status==P4_FILE_TRANSFER_STATUS_OK);
 snprintf(path,sizeof(path),"%s/TRANSFER/NOTE.TXT",root);f=fopen(path,"rb");assert(f);uint8_t saved[6];assert(fread(saved,1,sizeof(saved),f)==sizeof(payload));assert(memcmp(saved,payload,sizeof(payload))==0);assert(fclose(f)==0);assert(unlink(path)==0);
 snprintf(path,sizeof(path),"%s/TRANSFER/.P4FT",root);assert(rmdir(path)==0);
 snprintf(path,sizeof(path),"%s/TRANSFER",root);assert(rmdir(path)==0);snprintf(path,sizeof(path),"%s/P4/GAMES/OLD.P4CART",root);assert(unlink(path)==0);snprintf(path,sizeof(path),"%s/P4/GAMES",root);assert(rmdir(path)==0);snprintf(path,sizeof(path),"%s/P4",root);assert(rmdir(path)==0);assert(rmdir(root)==0);
 puts("retired class: 9 fragmented requests rejected; old archive unchanged; 3 native/resource/exchange routes retained; exchange upload/hash/activation passed");return 0;
}
