// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "platform/tab5.h"
#include "mock_sdk.h"
static uint8_t regs[32];
static unsigned input_reads, register_reads, resets, creates, deletes;
static unsigned init_failures, reset_failures;
static bool read_timeout, corrupt_latch, corrupt_direction, corrupt_drive;
const char *esp_err_to_name(int v) {(void)v;return "mock";}
void mock_log(const char *t,const char *f,...) {(void)t;(void)f;}
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *p) {return p;}
int xSemaphoreTake(SemaphoreHandle_t p,unsigned n) {(void)p;(void)n;return 1;}
void xSemaphoreGive(SemaphoreHandle_t p) {(void)p;}
void vTaskDelay(unsigned t) {(void)t;}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *c,i2c_master_bus_handle_t *o) {assert(c->sda_io_num==31&&c->scl_io_num==32);*o=regs;++creates;return 0;}
esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t b) {(void)b;++resets;if(reset_failures){--reset_failures;return ESP_ERR_INVALID_STATE;}return 0;}
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t b) {assert(b);++deletes;return 0;}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t b,const i2c_device_config_t *c,i2c_master_dev_handle_t *o) {assert(b&&c->device_address==0x43);*o=regs;return 0;}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t d,const uint8_t *w,size_t wn,uint8_t *r,size_t rn,int timeout) {
 assert(d&&wn==1&&rn==1&&timeout<=100);++register_reads;
 if(read_timeout)return ESP_ERR_TIMEOUT;
 assert(*w==3||*w==5||*w==7);*r=regs[*w];
 if(corrupt_latch&&*w==5)*r^=2;
 if(corrupt_direction&&*w==3)*r&=(uint8_t)~2U;
 if(corrupt_drive&&*w==7)*r|=2;
 return 0;
}
esp_err_t i2c_master_probe(i2c_master_bus_handle_t b,int a,int t) {(void)b;(void)a;(void)t;return ESP_FAIL;}
esp_err_t esp_io_expander_new_i2c_pi4ioe5v6408(i2c_master_bus_handle_t b,int a,esp_io_expander_handle_t *o) {assert(b&&a==0x43);if(init_failures){--init_failures;return ESP_ERR_TIMEOUT;}regs[3]=255;regs[5]=0;regs[7]=255;*o=regs;return 0;}
esp_err_t esp_io_expander_set_level(esp_io_expander_handle_t h,uint32_t p,int v) {assert(h&&(regs[3]&p)==p);if(v)regs[5]|=p;else regs[5]&=(uint8_t)~p;return 0;}
/* Datasheet: input status for an output is always zero, even when driven high. */
esp_err_t esp_io_expander_get_level(esp_io_expander_handle_t h,uint32_t p,uint32_t *v) {assert(h);++input_reads;*v=regs[15]&p&~regs[3];return 0;}
esp_err_t esp_io_expander_set_dir(esp_io_expander_handle_t h,uint32_t p,int v) {assert(h);if(v)regs[3]|=p;else regs[3]&=(uint8_t)~p;return 0;}
esp_err_t esp_io_expander_set_output_mode(esp_io_expander_handle_t h,uint32_t p,int v) {assert(h);if(v)regs[7]|=p;else regs[7]&=(uint8_t)~p;return 0;}
esp_err_t esp_io_expander_set_pullupdown(esp_io_expander_handle_t h,uint32_t p,int v) {(void)h;(void)p;(void)v;return 0;}
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t p) {(void)p;return 0;}
esp_err_t esp_lcd_new_panel_io_i2c(i2c_master_bus_handle_t b,const esp_lcd_panel_io_i2c_config_t *c,esp_lcd_panel_io_handle_t *o) {(void)b;(void)c;(void)o;return ESP_FAIL;}
esp_err_t esp_lcd_panel_io_rx_param(esp_lcd_panel_io_handle_t h,int c,void *d,size_t n) {(void)h;(void)c;(void)d;(void)n;return ESP_FAIL;}
int main(int argc, char **argv) {
 unsigned expected_resets=0,expected_deletes=0;
 if(argc>1) {
   if(!strcmp(argv[1],"retry")) {init_failures=1;expected_resets=1;}
   else if(!strcmp(argv[1],"recreate")) {init_failures=reset_failures=1;expected_resets=expected_deletes=1;}
   else if(!strcmp(argv[1],"bounded")) {
     init_failures=3;reset_failures=2;
     assert(platform_tab5_init()==ESP_ERR_TIMEOUT);
     assert(resets==2&&creates==3&&deletes==2);
     puts("Tab5 exhausted startup recovery stops after three attempts PASS");return 0;
   } else assert(!"unknown test mode");
 }
 assert(platform_tab5_init()==ESP_OK);
 assert(resets==expected_resets&&deletes==expected_deletes&&creates==1+expected_deletes);
 assert(platform_tab5_display_reset()==ESP_OK);
 uint8_t other_dir=regs[3]&~2U,other_latch=regs[5]&~2U,other_drive=regs[7]&~2U;
 assert(platform_tab5_speaker_enable(true)==ESP_OK);
 assert(regs[5]&2);assert(input_reads==0&&register_reads==3);
 assert((regs[3]&~2U)==other_dir&&(regs[5]&~2U)==other_latch&&(regs[7]&~2U)==other_drive);
 assert(platform_tab5_speaker_enable(false)==ESP_OK);assert(!(regs[5]&2));
 read_timeout=true;assert(platform_tab5_speaker_enable(true)==ESP_ERR_TIMEOUT);read_timeout=false;
 corrupt_latch=true;assert(platform_tab5_speaker_enable(true)==ESP_FAIL);corrupt_latch=false;
 corrupt_direction=true;assert(platform_tab5_speaker_enable(true)==ESP_FAIL);corrupt_direction=false;
 corrupt_drive=true;assert(platform_tab5_speaker_enable(true)==ESP_FAIL);corrupt_drive=false;
 assert(platform_tab5_speaker_enable(false)==ESP_OK);assert(resets==expected_resets&&input_reads==0);
 puts("Tab5 speaker: enable/disable, unchanged display controls, live readback faults PASS");
 return 0;
}
