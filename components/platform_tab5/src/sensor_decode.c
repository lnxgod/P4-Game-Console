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
