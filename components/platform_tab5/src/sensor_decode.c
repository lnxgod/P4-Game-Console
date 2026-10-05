// SPDX-License-Identifier: MIT
#include "platform/tab5_sensors.h"

static int32_t signed_word(uint16_t raw)
{
    return raw <= INT16_MAX ? (int32_t)raw : (int32_t)raw - 65536;
}

bool platform_tab5_decode_power(uint16_t bus_raw, uint16_t shunt_raw,
                                uint16_t *mv, int32_t *ma, uint8_t *percent)
{
    if (!mv || !ma || !percent || (bus_raw & 0x8000U)) return false;
    const uint32_t voltage = ((uint32_t)bus_raw * 5U + 2U) / 4U; /* 1.25 mV/LSB */
    *mv = (uint16_t)voltage;
    *ma = signed_word(shunt_raw) / 2; /* 2.5 uV/LSB over the schematic's 5 mOhm shunt. */
    *percent = 0;
    if (voltage < 5000U || voltage > 9000U) return false;
    /* Same 2-cell voltage estimate as M5Unified; this is not a fuel gauge. */
    *percent = voltage <= 6600U ? 0U : voltage >= 8200U ? 100U :
        (uint8_t)((voltage - 6600U) * 100U / 1600U);
    return true;
}

static bool bcd(uint8_t raw, uint8_t maximum, uint8_t *out)
{
    if ((raw & 15U) > 9U || (raw >> 4U) > 9U) return false;
    *out = (uint8_t)((raw >> 4U) * 10U + (raw & 15U));
    return *out <= maximum;
}

bool platform_tab5_decode_rtc(const uint8_t r[16], platform_tab5_datetime_t *out)
{
    if (!r || !out || (r[13] & 0x02U) || (r[14] & 0x40U)) return false;
    platform_tab5_datetime_t t = {0};
    uint8_t year;
    if (!bcd(r[0] & 0x7fU, 59, &t.second) || !bcd(r[1] & 0x7fU, 59, &t.minute) ||
        !bcd(r[2] & 0x3fU, 23, &t.hour) || !bcd(r[4] & 0x3fU, 31, &t.day) ||
        !bcd(r[5] & 0x1fU, 12, &t.month) || !bcd(r[6], 99, &year) ||
        !t.day || !t.month || !r[3] || (r[3] & 0x80U) || (r[3] & (r[3] - 1U))) return false;
    t.year = (uint16_t)(2000U + year);
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const unsigned max_day = days[t.month - 1U] + (t.month == 2U && year % 4U == 0U ? 1U : 0U);
    if (t.day > max_day) return false;
    *out = t;
    return true;
}

void platform_tab5_decode_motion(const uint8_t raw[12], int32_t acc[3], int32_t gyro[3])
{
    if (!raw || !acc || !gyro) return;
    for (unsigned i = 0; i < 3; ++i) {
        const int32_t a = signed_word((uint16_t)(raw[2*i] | (uint16_t)raw[2*i+1] << 8));
        const int32_t g = signed_word((uint16_t)(raw[6+2*i] | (uint16_t)raw[7+2*i] << 8));
        acc[i] = a * 4000 / 32768; /* configured +/-4 g */
        gyro[i] = (int32_t)((int64_t)g * 2000000 / 32768); /* +/-2000 deg/s */
    }
}

/* The RX8130 calendar represents 2000..2099, all within unsigned Unix time. */
static unsigned month_days(unsigned year, unsigned month)
{
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month - 1U] + (month == 2U && year % 4U == 0U ? 1U : 0U);
}
bool platform_tab5_datetime_to_unix(const platform_tab5_datetime_t *d, uint32_t *out)
{
    if (!d || !out || d->year < 2000U || d->year > 2099U ||
        d->month < 1U || d->month > 12U || d->day < 1U ||
        d->day > month_days(d->year, d->month) ||
        d->hour > 23U || d->minute > 59U || d->second > 59U) return false;
    uint32_t days = 0;
    for (unsigned y = 2000U; y < d->year; ++y) days += y % 4U == 0U ? 366U : 365U;
    for (unsigned m = 1U; m < d->month; ++m) days += month_days(d->year, m);
    days += (uint32_t)d->day - 1U;
    *out = UINT32_C(946684800) + days * 86400U +
        (uint32_t)d->hour * 3600U + (uint32_t)d->minute * 60U + d->second;
    return true;
}
bool platform_tab5_datetime_from_unix(uint32_t seconds, platform_tab5_datetime_t *out)
{
    if (!out || seconds < UINT32_C(946684800) || seconds > UINT32_C(4102444799)) return false;
    uint32_t elapsed = seconds - UINT32_C(946684800);
    uint32_t days = elapsed / 86400U;
    platform_tab5_datetime_t d = {.year=2000U,.month=1U};
    while (days >= (d.year % 4U == 0U ? 366U : 365U)) {
        days -= d.year % 4U == 0U ? 366U : 365U; ++d.year;
    }
    while (days >= month_days(d.year, d.month)) {
        days -= month_days(d.year, d.month); ++d.month;
    }
    d.day = (uint8_t)(days + 1U);
    d.hour = (uint8_t)(elapsed % 86400U / 3600U);
    d.minute = (uint8_t)(elapsed % 3600U / 60U);
    d.second = (uint8_t)(elapsed % 60U);
    *out = d;
    return true;
}
static uint8_t to_bcd(unsigned value)
{
    return (uint8_t)((value / 10U << 4U) | (value % 10U));
}
bool platform_tab5_encode_rtc(uint32_t seconds, uint8_t r[7])
{
    platform_tab5_datetime_t d;
    if (!r || !platform_tab5_datetime_from_unix(seconds, &d)) return false;
    r[0]=to_bcd(d.second); r[1]=to_bcd(d.minute); r[2]=to_bcd(d.hour);
    r[3]=(uint8_t)(1U << ((seconds / 86400U + 4U) % 7U)); /* Sunday bit 0. */
    r[4]=to_bcd(d.day); r[5]=to_bcd(d.month); r[6]=to_bcd(d.year - 2000U);
    return true;
}
