// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
} platform_tab5_datetime_t;

typedef struct {
    uint64_t sampled_us;
    bool battery_ready, battery_valid, imu_ready, imu_valid, rtc_present, rtc_valid;
    uint16_t battery_mv;
    int32_t battery_ma; /* Positive discharge, negative charge, per Tab5 shunt wiring. */
    uint8_t battery_percent;
    int32_t accel_mg[3], gyro_mdps[3]; /* Physical sensor axes, no auto rotation. */
    int32_t temperature_mc;
    bool temperature_valid;
    platform_tab5_datetime_t rtc;
    int battery_error, imu_error, rtc_error;
} platform_tab5_telemetry_t;

/* Pure, bounded conversions, independently tested on the host. */
bool platform_tab5_decode_power(uint16_t bus_raw, uint16_t shunt_raw,
                                uint16_t *millivolts, int32_t *milliamps, uint8_t *percent);
bool platform_tab5_decode_rtc(const uint8_t registers[16], platform_tab5_datetime_t *out);
void platform_tab5_decode_motion(const uint8_t raw[12], int32_t accel_mg[3], int32_t gyro_mdps[3]);

#ifdef ESP_PLATFORM
#include "esp_err.h"
/* Board-owned background sampler; does not delay launcher initialization.
 * The persistent shared bus remains owned by platform_tab5, including in Doom. */
esp_err_t platform_tab5_sensors_start(void);
platform_tab5_telemetry_t platform_tab5_sensors_snapshot(void);
#endif
