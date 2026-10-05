// SPDX-License-Identifier: MIT
#include "platform/tab5_sensors.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    uint16_t mv = 77; int32_t ma = 77; uint8_t percent = 77;
    assert(platform_tab5_decode_power(5920, 200, &mv, &ma, &percent));
    assert(mv == 7400 && ma == 100 && percent == 50);
    assert(platform_tab5_decode_power(6560, 0xff38, &mv, &ma, &percent));
    assert(mv == 8200 && ma == -100 && percent == 100);
    assert(platform_tab5_decode_power(4800, 0x8000, &mv, &ma, &percent));
    assert(mv == 6000 && ma == -16384 && percent == 0);
    assert(!platform_tab5_decode_power(0, 0, &mv, &ma, &percent));
    assert(mv == 0 && ma == 0 && percent == 0);
    assert(!platform_tab5_decode_power(0xffff, 0, &mv, &ma, &percent));
    assert(!platform_tab5_decode_power(7400, 0, NULL, &ma, &percent));
    uint8_t rtc[16] = {0x59,0x58,0x23,0x01,0x29,0x02,0x24};
    platform_tab5_datetime_t dt = {0};
    assert(platform_tab5_decode_rtc(rtc, &dt));
    assert(dt.year == 2024 && dt.month == 2 && dt.day == 29 && dt.hour == 23);
    rtc[6] = 0x25; assert(!platform_tab5_decode_rtc(rtc,&dt)); rtc[6] = 0x24;
    rtc[0] = 0x5a; assert(!platform_tab5_decode_rtc(rtc,&dt)); rtc[0] = 0x59;
    rtc[13] = 2; assert(!platform_tab5_decode_rtc(rtc,&dt)); rtc[13] = 0;
    rtc[14] = 0x40; assert(!platform_tab5_decode_rtc(rtc,&dt)); rtc[14] = 0;
    rtc[3] = 0; assert(!platform_tab5_decode_rtc(rtc,&dt));
    rtc[3] = 3; assert(!platform_tab5_decode_rtc(rtc,&dt));
    uint8_t raw[12] = {0,0x20,0,0x80,0xff,0x7f,0,0x40,0,0x80,0,0};
    int32_t acc[3], gyro[3]; platform_tab5_decode_motion(raw,acc,gyro);
    assert(acc[0] == 1000 && acc[1] == -4000 && acc[2] == 3999);
    assert(gyro[0] == 1000000 && gyro[1] == -2000000 && gyro[2] == 0);
    puts("Tab5 sensor conversion, missing voltage, signed current, RTC validity and full-scale motion PASS");
    return 0;
}
