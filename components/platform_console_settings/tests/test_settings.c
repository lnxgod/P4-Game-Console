// SPDX-License-Identifier: MIT
#include "platform/console_settings.h"
#include "sdk.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {char key[24];uint8_t data[128];size_t size;} entry;
static entry committed[12],working[12];
static bool fail_init,fail_commit;
void test_log(const char *tag,const char *format,...){(void)tag;(void)format;}
const char *esp_err_to_name(esp_err_t error){(void)error;return "test";}
esp_err_t esp_read_mac(uint8_t *mac,int type){(void)type;memset(mac,1,6);return ESP_OK;}
esp_err_t nvs_flash_init(void){return fail_init?ESP_FAIL:ESP_OK;}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h){assert(strcmp(name,"p4_console")==0);(void)mode;*h=1;memcpy(working,committed,sizeof(working));return ESP_OK;}
void nvs_close(nvs_handle_t h){assert(h==1);}
static entry *find(const char *key,bool create){
    for(unsigned i=0;i<12;++i)if(strcmp(working[i].key,key)==0)return &working[i];
    if(create)for(unsigned i=0;i<12;++i)if(!working[i].key[0]){snprintf(working[i].key,sizeof(working[i].key),"%s",key);return &working[i];}
    return NULL;
}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size){
    (void)h;entry *e=find(key,false);if(!e)return ESP_ERR_NVS_NOT_FOUND;
    if(*size<e->size){*size=e->size;return ESP_ERR_NVS_INVALID_LENGTH;}
    memcpy(out,e->data,e->size);*size=e->size;return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size){
    (void)h;entry *e=find(key,true);assert(e&&size<=sizeof(e->data));memcpy(e->data,data,size);e->size=size;return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *out){size_t size=1;return nvs_get_blob(h,key,out,&size);}
esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t value){return nvs_set_blob(h,key,&value,1);}
esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *size){return nvs_get_blob(h,key,out,size);}
esp_err_t nvs_set_str(nvs_handle_t h,const char *key,const char *value){return nvs_set_blob(h,key,value,strlen(value)+1);}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;if(fail_commit)return ESP_FAIL;memcpy(committed,working,sizeof(committed));return ESP_OK;}
int main(void){
    platform_console_settings_t s;
    assert(platform_console_settings_init(&s)==ESP_OK);
    assert(s.boot_volume_step==3&&s.game_volume_step==3&&s.persistent);
    assert(platform_console_settings_set_node_name(&s,"TEST-ROOM")==ESP_OK);
    assert(platform_console_settings_set_ble_controller_enabled(&s,false)==ESP_OK);
    /* Migrate a previously muted device once, preserving unrelated settings. */
    nvs_handle_t h;assert(nvs_open("p4_console",NVS_READWRITE,&h)==ESP_OK);
    assert(nvs_set_u8(h,"volume_policy",3)==ESP_OK);assert(nvs_set_u8(h,"boot_volume",0)==ESP_OK);assert(nvs_set_u8(h,"game_volume",0)==ESP_OK);assert(nvs_commit(h)==ESP_OK);nvs_close(h);
    assert(platform_console_settings_init(&s)==ESP_OK);assert(s.boot_volume_step==3&&s.game_volume_step==3);
    assert(strcmp(s.node_name,"TEST-ROOM")==0&&!s.ble_controller_enabled);
    assert(platform_console_settings_set_boot_volume(&s,2)==ESP_OK);assert(platform_console_settings_set_game_volume(&s,0)==ESP_OK);
    assert(platform_console_settings_init(&s)==ESP_OK);assert(s.boot_volume_step==2&&s.game_volume_step==0);
    assert(platform_console_settings_set_game_volume(&s,11)==ESP_ERR_INVALID_ARG);
    fail_commit=true;assert(platform_console_settings_set_game_volume(&s,7)==ESP_FAIL);assert(s.game_volume_step==0);fail_commit=false;
    fail_init=true;assert(platform_console_settings_init(&s)==ESP_FAIL);assert(!s.persistent&&s.boot_volume_step==3&&s.game_volume_step==3);fail_init=false;
    assert(platform_console_settings_init(&s)==ESP_OK);assert(s.boot_volume_step==2&&s.game_volume_step==0);
    puts("SETTINGS PASS fresh=3 legacy-mute-migrated=3 later-edits-preserved=1 unrelated-settings-preserved=1 errors-bounded=1");
    return 0;
}
