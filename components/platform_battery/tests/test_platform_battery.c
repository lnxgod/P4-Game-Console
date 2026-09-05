#include "platform_battery/battery.h"

#include <assert.h>

int main(void)
{
    assert(platform_battery_voltage_to_percent(3300U, 3300U, 4200U) == 0U);
    assert(platform_battery_voltage_to_percent(4200U, 3300U, 4200U) == 100U);
    assert(platform_battery_voltage_to_percent(3750U, 3300U, 4200U) == 50U);
    assert(platform_battery_voltage_to_percent(3000U, 3300U, 4200U) == 0U);
    assert(platform_battery_voltage_to_percent(4500U, 3300U, 4200U) == 100U);
    assert(platform_battery_voltage_to_percent(3800U, 4200U, 3300U) == 0U);
    assert(platform_battery_adc_mv_to_battery_mv(1100U) == 3300U);
    assert(platform_battery_adc_mv_to_battery_mv(1400U) == 4200U);
    return 0;
}
