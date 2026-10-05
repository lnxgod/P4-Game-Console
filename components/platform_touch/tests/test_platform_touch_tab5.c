#include "platform/touch.h"
#include "platform/tab5.h"
#include "esp_lcd_touch_st7123.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static int bus_token,io_token,driver_token;
static platform_tab5_panel_t panel;
static int driver_kind,pin_mode;
static uint32_t address;
static esp_err_t create_result,io_delete_result,driver_delete_result,read_result,data_result;
static uint8_t points_count;
static uint8_t gt_status=0x80;
static size_t report_reads;
static esp_lcd_touch_point_data_t points[5];
i2c_master_bus_handle_t platform_tab5_i2c(void) { return (void *)&bus_token; }
esp_err_t platform_tab5_panel_detect(platform_tab5_panel_t *out) { *out=panel;return panel?ESP_OK:ESP_ERR_NOT_SUPPORTED; }
esp_err_t gpio_config(const gpio_config_t *cfg) { assert(cfg->pin_bit_mask==(UINT64_C(1)<<23));pin_mode=cfg->mode;return ESP_OK; }
esp_err_t gpio_set_level(gpio_num_t pin,unsigned level) { assert(pin==23 && level==0);return ESP_OK; }
int64_t esp_timer_get_time(void) { return 12345; }
esp_err_t esp_lcd_new_panel_io_i2c(i2c_master_bus_handle_t bus,const esp_lcd_panel_io_i2c_config_t *cfg,esp_lcd_panel_io_handle_t *out)
{ assert(bus==platform_tab5_i2c());address=cfg->dev_addr;*out=(void *)&io_token;return ESP_OK; }
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io) { assert(io==(void *)&io_token);return io_delete_result; }
esp_err_t esp_lcd_panel_io_rx_param(esp_lcd_panel_io_handle_t io,int reg,void *out,size_t bytes)
{
    assert(io==(void *)&io_token);
    if (read_result!=ESP_OK) return read_result;
    uint8_t *p=out;
    if (reg==0x814e) { assert(bytes==1);p[0]=gt_status; }
    else if (reg==0x10) { assert(bytes==1);p[0]=0x08; }
    else if (reg==0x09) { assert(bytes==1);p[0]=points_count; }
    else {
        assert(reg==0x14 && bytes<=70);++report_reads;
        if (data_result!=ESP_OK) return data_result;
        memset(p,0,bytes);
        for (unsigned i=0;i<points_count && i<5;++i) {
            p[i*7]=(uint8_t)(0x80U|(points[i].x>>8));p[i*7+1]=(uint8_t)points[i].x;
            p[i*7+2]=(uint8_t)(points[i].y>>8);p[i*7+3]=(uint8_t)points[i].y;
        }
    }
    return ESP_OK;
}
static esp_err_t create_driver(esp_lcd_panel_io_handle_t io,const esp_lcd_touch_config_t *cfg,esp_lcd_touch_handle_t *out,int kind)
{
    assert(io==(void *)&io_token && cfg->rst_gpio_num==-1 && cfg->int_gpio_num==-1);
    assert(cfg->x_max==720 && cfg->y_max==1280);
    driver_kind=kind;if(create_result==ESP_OK)*out=(void *)&driver_token;return create_result;
}
esp_err_t esp_lcd_touch_new_i2c_gt911(esp_lcd_panel_io_handle_t io,const esp_lcd_touch_config_t *cfg,esp_lcd_touch_handle_t *out)
{ return create_driver(io,cfg,out,1); }
esp_err_t esp_lcd_touch_new_i2c_st7123(esp_lcd_panel_io_handle_t io,const esp_lcd_touch_config_t *cfg,esp_lcd_touch_handle_t *out)
{ return create_driver(io,cfg,out,2); }
esp_err_t esp_lcd_touch_del(esp_lcd_touch_handle_t t) { assert(t==(void *)&driver_token);return driver_delete_result; }
esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t t) { assert(t==(void *)&driver_token && panel==TAB5_PANEL_ILI9881C);return read_result; }
esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t t,esp_lcd_touch_point_data_t *out,uint8_t *count,uint8_t max)
{ assert(t==(void *)&driver_token && max==5);memcpy(out,points,sizeof(points));*count=points_count;return data_result; }

static void neutral_error(platform_touch_t *t)
{
    platform_touch_frame_t f;memset(&f,0xa5,sizeof(f));
    assert(platform_touch_poll(t,&f)!=ESP_OK);
    assert(!f.valid && !f.contact_count && !f.timestamp_us);
    for(unsigned i=0;i<5;++i)assert(!f.contacts[i].x && !f.contacts[i].y);
}
int main(void)
{
    platform_touch_t *t=NULL,*second=NULL;
    platform_touch_config_t cfg;platform_touch_config_init(&cfg,platform_tab5_i2c());
    assert(platform_touch_create(&cfg,&t)==ESP_ERR_NOT_SUPPORTED && !t);
    for(int kind=TAB5_PANEL_ILI9881C;kind<=TAB5_PANEL_ST7121;++kind) {
        panel=(platform_tab5_panel_t)kind;
        assert(platform_touch_create(&cfg,&t)==ESP_OK && t);
        assert(address==(kind==TAB5_PANEL_ILI9881C?0x14U:0x55U));
        assert(driver_kind==(kind==TAB5_PANEL_ILI9881C?1:2));
        assert(pin_mode==(kind==TAB5_PANEL_ILI9881C?GPIO_MODE_OUTPUT:GPIO_MODE_INPUT));
        assert(platform_touch_create(&cfg,&second)==ESP_ERR_INVALID_STATE && !second);
        points_count=2;points[0]=(esp_lcd_touch_point_data_t){.x=0,.y=0};
        points[1]=(esp_lcd_touch_point_data_t){.x=719,.y=1279};
        platform_touch_frame_t f;
        assert(platform_touch_poll(t,&f)==ESP_OK && f.valid && f.contact_count==2);
        assert(f.contacts[0].x==1279 && f.contacts[0].y==0);
        assert(f.contacts[1].x==0 && f.contacts[1].y==719 && f.timestamp_us==12345);
        if (kind==TAB5_PANEL_ILI9881C) {
            gt_status=0;assert(platform_touch_poll(t,&f)==ESP_OK && f.contact_count==2 && f.timestamp_us==12345);gt_status=0x80;
        }
        read_result=ESP_FAIL;neutral_error(t);read_result=ESP_OK;
        if (kind==TAB5_PANEL_ILI9881C) {
            gt_status=0;assert(platform_touch_poll(t,&f)==ESP_OK && f.contact_count==0);gt_status=0x80;
        }
        data_result=ESP_FAIL;neutral_error(t);data_result=ESP_OK;
        const size_t before=report_reads;
        points_count=kind==TAB5_PANEL_ILI9881C?6:255;neutral_error(t);
        assert(report_reads==before);points_count=1;
        points[0].x=720;neutral_error(t);points[0].x=0;
        points_count=0;assert(platform_touch_poll(t,&f)==ESP_OK && f.valid && !f.timestamp_us);
        driver_delete_result=ESP_FAIL;assert(platform_touch_destroy(&t)!=ESP_OK && t);neutral_error(t);
        driver_delete_result=ESP_OK;io_delete_result=ESP_FAIL;
        assert(platform_touch_destroy(&t)!=ESP_OK && t);neutral_error(t);
        io_delete_result=ESP_OK;assert(platform_touch_destroy(&t)==ESP_OK && !t);
    }
    create_result=ESP_FAIL;io_delete_result=ESP_FAIL;
    assert(platform_touch_create(&cfg,&t)!=ESP_OK && t);neutral_error(t);
    io_delete_result=ESP_OK;assert(platform_touch_destroy(&t)==ESP_OK && !t);
    puts("TAB5 TOUCH PASS variants=3 malformed-input=neutral teardown=retryable");return 0;
}
