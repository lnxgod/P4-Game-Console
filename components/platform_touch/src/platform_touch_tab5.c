// SPDX-License-Identifier: MIT
#include "platform/touch.h"
#include "platform/tab5.h"
#include "platform_touch_frame.h"
#include "platform_touch_tab5_io.h"
#include "platform_touch_sampler_private.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_touch_st7123.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>

struct platform_touch {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_touch_handle_t driver;
    uint32_t sequence;
    bool gt911;
    bool ready;
    bool sampler_claimed;
    platform_touch_frame_t last;
};
static platform_touch_t *s_owner;

void platform_touch_config_init(platform_touch_config_t *cfg,
    i2c_master_bus_handle_t bus)
{
    if (cfg) *cfg = (platform_touch_config_t){.bus=bus,.address_7bit=0x14};
}
esp_err_t platform_touch_destroy(platform_touch_t **touch)
{
    if (!touch || !*touch || *touch != s_owner) return ESP_ERR_INVALID_ARG;
    platform_touch_t *t=*touch;
    if (t->sampler_claimed) return ESP_ERR_INVALID_STATE;
    t->ready=false;
    esp_err_t ret;
    if (t->driver) {
        ret=esp_lcd_touch_del(t->driver);
        if (ret!=ESP_OK) return ret;
        t->driver=NULL;
    }
    if (t->io) {
        ret=esp_lcd_panel_io_del(t->io);
        if (ret!=ESP_OK) return ret;
        t->io=NULL;
    }
    free(t); *touch=NULL; s_owner=NULL;
    return ESP_OK;
}
esp_err_t platform_touch_create(const platform_touch_config_t *cfg,
    platform_touch_t **out)
{
    if (!cfg || !out || *out || !cfg->bus || cfg->bus!=platform_tab5_i2c()) return ESP_ERR_INVALID_ARG;
    if (s_owner) return ESP_ERR_INVALID_STATE;
    platform_tab5_panel_t panel;
    esp_err_t ret=platform_tab5_panel_detect(&panel);
    if (ret!=ESP_OK) return ret;
    const bool ili=panel==TAB5_PANEL_ILI9881C;
    const gpio_config_t pin={.pin_bit_mask=UINT64_C(1)<<23,
        .mode=ili?GPIO_MODE_OUTPUT:GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE,
        .intr_type=GPIO_INTR_DISABLE};
    /* Official BSP resistor workaround applies only to the GT911 assembly. */
    if (ili) { ret=gpio_set_level(GPIO_NUM_23,0); if (ret!=ESP_OK) return ret; }
    ret=gpio_config(&pin); if (ret!=ESP_OK) return ret;
    platform_touch_t *t=calloc(1,sizeof(*t));
    if (!t) return ESP_ERR_NO_MEM;
    s_owner=t;
    t->gt911=ili;
    platform_touch_frame_neutral(&t->last);
    _Static_assert(PLATFORM_TOUCH_I2C_CLOCK_HZ==PLATFORM_TOUCH_TAB5_IO_CLOCK_HZ,
        "Tab5 touch adapter must preserve the platform I2C clock");
    const uint8_t address=ili?0x14U:ESP_LCD_TOUCH_IO_I2C_ST7123_ADDRESS;
    const esp_lcd_touch_config_t config={.x_max=720,.y_max=1280,
        .rst_gpio_num=GPIO_NUM_NC,.int_gpio_num=GPIO_NUM_NC};
    ret=platform_touch_tab5_io_create(cfg->bus,address,&t->io);
    if (ret==ESP_OK) ret=ili?esp_lcd_touch_new_i2c_gt911(t->io,&config,&t->driver):esp_lcd_touch_new_i2c_st7123(t->io,&config,&t->driver);
    t->ready=ret==ESP_OK;
    *out=t;
    if (ret!=ESP_OK) (void)platform_touch_destroy(out); /* Retain failed cleanup ownership. */
    return ret;
}
/* ST712x register protocol from the locked official 1.0.2 component.
 * Its read_data callback trusts the device's count before reading a fixed stack
 * array. Keep that callback unreachable here and bound the transfer ourselves. */
static esp_err_t read_st712x(platform_touch_t *t, uint16_t *x, uint16_t *y,
    uint16_t *strength, uint8_t *count)
{
    uint8_t info=0,slots=0,report[10*7]={0};
    esp_err_t ret=esp_lcd_panel_io_rx_param(t->io,0x0010,&info,1);
    if (ret!=ESP_OK || !(info&0x08U)) return ret;
    ret=esp_lcd_panel_io_rx_param(t->io,0x0009,&slots,1);
    if (ret!=ESP_OK) return ret;
    if (slots>10) return ESP_ERR_INVALID_RESPONSE;
    if (!slots) return ESP_OK;
    ret=esp_lcd_panel_io_rx_param(t->io,0x0014,report,(size_t)slots*7U);
    if (ret!=ESP_OK) return ret;
    for (uint8_t i=0;i<slots;++i) {
        const uint8_t *p=&report[(size_t)i*7U];
        if (!(p[0]&0x80U)) continue;
        const uint16_t px=(uint16_t)(((uint16_t)(p[0]&0x3fU)<<8)|p[1]);
        const uint16_t py=(uint16_t)(((uint16_t)p[2]<<8)|p[3]);
        if (px>=720 || py>=1280) return ESP_ERR_INVALID_RESPONSE;
        if (*count<5) { x[*count]=px;y[*count]=py;strength[*count]=p[4];++*count; }
    }
    return ESP_OK;
}
static esp_err_t poll_hardware(platform_touch_t *t,platform_touch_frame_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    platform_touch_frame_fail_closed(out,0,0);
    if (!t || t!=s_owner || !t->ready || !t->driver) return ESP_ERR_INVALID_STATE;
    const uint32_t sequence=++t->sequence;
    platform_touch_frame_fail_closed(out,sequence,0);
    esp_err_t ret=ESP_OK;
    uint16_t x[5]={0},y[5]={0},strength[5]={0}; uint8_t count=0;
    if (t->gt911) {
        uint8_t status=0;
        ret=esp_lcd_panel_io_rx_param(t->io,0x814e,&status,1);
        if (ret!=ESP_OK) goto fail;
        if (!(status&0x80U)) {
            /* Repeated reports keep their acquisition timestamp. After an I/O
             * fault, last is neutral so stale driver data cannot reassert input. */
            *out=t->last;out->sequence=sequence;return ESP_OK;
        }
        ret=esp_lcd_touch_read_data(t->driver);
        if (ret!=ESP_OK) goto fail;
        esp_lcd_touch_point_data_t points[5]={0};
        ret=esp_lcd_touch_get_data(t->driver,points,&count,5);
        if (ret!=ESP_OK) goto fail;
        if (count>5) { ret=ESP_ERR_INVALID_RESPONSE;goto fail; }
        for (uint8_t i=0;i<count;++i) { x[i]=points[i].x;y[i]=points[i].y;strength[i]=points[i].strength; }
    } else {
        ret=read_st712x(t,x,y,strength,&count);
        if (ret!=ESP_OK) goto fail;
    }
    if (!platform_touch_coordinates_native_to_logical(x,y,count) ||
        !platform_touch_frame_from_raw(out,sequence,count?esp_timer_get_time():0,x,y,strength,count)) {
        ret=ESP_ERR_INVALID_RESPONSE;goto fail;
    }
    t->last=*out;
    return ESP_OK;
fail:
    platform_touch_frame_fail_closed(out,sequence,0);
    platform_touch_frame_neutral(&t->last);
    return ret;
}
esp_err_t platform_touch_gt911_read_info(platform_touch_t *t,platform_touch_gt911_info_t *out)
{
    (void)t;
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out,0,sizeof(*out)); return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t platform_touch_gt911_restore_reviewed_baseline(platform_touch_t *t,
    const platform_touch_gt911_restore_reviewed_baseline_request_t *req,
    platform_touch_gt911_restore_reviewed_baseline_result_t *out)
{
    (void)t;(void)req;
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out,0,sizeof(*out)); out->result=ESP_ERR_NOT_SUPPORTED;
    return ESP_ERR_NOT_SUPPORTED;
}


esp_err_t platform_touch_sampler_claim(platform_touch_t *t)
{
    if (!t || t!=s_owner || !t->ready || !t->driver || t->sampler_claimed)
        return ESP_ERR_INVALID_STATE;
    t->sampler_claimed=true;
    return ESP_OK;
}
esp_err_t platform_touch_sampler_release(platform_touch_t *t)
{
    if (!t || t!=s_owner || !t->sampler_claimed) return ESP_ERR_INVALID_STATE;
    t->sampler_claimed=false;
    return ESP_OK;
}
esp_err_t platform_touch_poll_sampled(platform_touch_t *t,platform_touch_frame_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    platform_touch_frame_fail_closed(out,0,0);
    if (!t || t!=s_owner || !t->sampler_claimed) return ESP_ERR_INVALID_STATE;
    return poll_hardware(t,out);
}
esp_err_t platform_touch_poll(platform_touch_t *t,platform_touch_frame_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    platform_touch_frame_fail_closed(out,0,0);
    if (!t || t!=s_owner || t->sampler_claimed) return ESP_ERR_INVALID_STATE;
    return poll_hardware(t,out);
}
