// SPDX-License-Identifier: MIT
#include "../src/rtc_clock.h"
#include "platform/tab5_sensors.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t regs[256];
static unsigned calls, fail_call, writes, flag_writes;
static bool corrupt_calendar, ignore_clear;
static int token;
static void reset(void)
{
    memset(regs,0,sizeof(regs)); regs[0x1d]=0xbf; regs[0x1e]=0x40;
    regs[0x1c]=0x04; regs[0x1f]=0x30;
    calls=fail_call=writes=flag_writes=0; corrupt_calendar=ignore_clear=false;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t d,const uint8_t *r,size_t n,uint8_t *out,size_t size,int timeout)
{
    assert(d==&token && n==1 && timeout==50 && *r+size<=256);
    if (++calls==fail_call) return ESP_ERR_TIMEOUT;
    memcpy(out,regs+*r,size);
    if (corrupt_calendar && writes && *r==0x10) out[4]=0x31;
    return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t d,const uint8_t *data,size_t size,int timeout)
{
    assert(d==&token && size>=2 && data[0]+size-1<=256 && timeout==50);
    if (++calls==fail_call) return ESP_ERR_TIMEOUT;
    ++writes;
    if (data[0]==0x1d) {
        ++flag_writes;
        if (!ignore_clear) regs[0x1d]&=(uint8_t)(data[1]|1U);
    } else memcpy(regs+data[0],data+1,size-1);
    return ESP_OK;
}
int main(void)
{
    /* Cross 2038, every leap day, all year/month boundaries, and both limits. */
    for (uint64_t t=UINT64_C(946684800);t<=UINT64_C(4102444799);t+=86400U) {
        platform_tab5_datetime_t d; uint32_t back; uint8_t raw[16]={0};
        assert(platform_tab5_datetime_from_unix((uint32_t)t,&d));
        assert(platform_tab5_datetime_to_unix(&d,&back) && back==t);
        assert(platform_tab5_encode_rtc((uint32_t)t,raw));
        assert(platform_tab5_decode_rtc(raw,&d));
        assert(platform_tab5_datetime_to_unix(&d,&back) && back==t);
        assert(raw[3]==(uint8_t)(1U<<((t/86400U+4U)%7U)));
    }
    platform_tab5_datetime_t d;uint32_t back;
    assert(platform_tab5_datetime_from_unix(UINT32_C(4102444799),&d));
    assert(d.year==2099 && d.month==12 && d.day==31 && d.hour==23 && d.minute==59 && d.second==59);
    assert(platform_tab5_datetime_to_unix(&d,&back) && back==UINT32_C(4102444799));
    assert(!platform_tab5_datetime_from_unix(0,&d));
    assert(!platform_tab5_datetime_from_unix(UINT32_C(4102444800),&d));
    d=(platform_tab5_datetime_t){.year=2025,.month=2,.day=29};
    assert(!platform_tab5_datetime_to_unix(&d,&back));
    const uint32_t now=UINT32_C(1709251139); /* 2024-02-29 23:58:59Z */
    reset();assert(platform_tab5_rtc_write(&token,now)==ESP_OK);
    assert(regs[0x1d]==0xbd && regs[0x1e]==0 && regs[0x1c]==4 && regs[0x1f]==0x30);
    assert(platform_tab5_decode_rtc(regs+0x10,&d));
    assert(platform_tab5_datetime_to_unix(&d,&back) && back==now);
    for (unsigned n=1;n<=6;++n) {
        reset();fail_call=n;
        assert(platform_tab5_rtc_write(&token,now)!=ESP_OK);
    }
    reset();corrupt_calendar=true;
    assert(platform_tab5_rtc_write(&token,now)==ESP_ERR_INVALID_RESPONSE && flag_writes==0);
    reset();ignore_clear=true;
    assert(platform_tab5_rtc_write(&token,now)==ESP_ERR_INVALID_RESPONSE);
    reset();regs[0x1e]=0x80;
    assert(platform_tab5_rtc_write(&token,now)==ESP_ERR_INVALID_STATE && writes==0);
    reset();assert(platform_tab5_rtc_write(&token,0)==ESP_ERR_INVALID_ARG && calls==0);
    puts("RTC calendar, readback, flag preservation and failure tests passed");
}
