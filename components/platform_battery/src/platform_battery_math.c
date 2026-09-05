// SPDX-License-Identifier: MIT

#include "platform_battery/battery.h"

#include <limits.h>

uint8_t platform_battery_voltage_to_percent(
    uint32_t battery_mv, uint32_t empty_mv, uint32_t full_mv)
{
    if (full_mv <= empty_mv || battery_mv <= empty_mv) {
        return 0U;
    }
    if (battery_mv >= full_mv) {
        return 100U;
    }
    const uint32_t span = full_mv - empty_mv;
    const uint32_t offset = battery_mv - empty_mv;
    /* Rounded integer arithmetic; the bounds above make this overflow-safe. */
    const uint64_t scaled = (uint64_t)offset * 100U + span / 2U;
    const uint64_t result = scaled / span;
    return result > UINT8_MAX ? UINT8_MAX : (uint8_t)result;
}

uint32_t platform_battery_adc_mv_to_battery_mv(uint32_t adc_mv)
{
    const uint64_t scaled = (uint64_t)adc_mv *
        (PLATFORM_BATTERY_DIVIDER_TOP_OHMS +
         PLATFORM_BATTERY_DIVIDER_BOTTOM_OHMS);
    const uint64_t result = (scaled +
        PLATFORM_BATTERY_DIVIDER_BOTTOM_OHMS / 2U) /
        PLATFORM_BATTERY_DIVIDER_BOTTOM_OHMS;
    return result > UINT32_MAX ? UINT32_MAX : (uint32_t)result;
}
