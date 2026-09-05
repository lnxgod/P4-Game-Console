// SPDX-License-Identifier: MIT

#ifndef PLATFORM_BATTERY_BATTERY_H
#define PLATFORM_BATTERY_BATTERY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 schematic contract. */
#define PLATFORM_BATTERY_ADC_GPIO 20U
#define PLATFORM_BATTERY_ADC1_CHANNEL 4U
#define PLATFORM_BATTERY_DIVIDER_TOP_OHMS 200000U
#define PLATFORM_BATTERY_DIVIDER_BOTTOM_OHMS 100000U
#define PLATFORM_BATTERY_DEFAULT_EMPTY_MV 3300U
#define PLATFORM_BATTERY_DEFAULT_FULL_MV 4200U

/** Clamp a battery voltage to the configured linear Li-ion range. */
uint8_t platform_battery_voltage_to_percent(
    uint32_t battery_mv, uint32_t empty_mv, uint32_t full_mv);

/** Convert calibrated BAT_ADC pin voltage through the 200k/100k divider. */
uint32_t platform_battery_adc_mv_to_battery_mv(uint32_t adc_mv);

#ifdef ESP_PLATFORM

typedef struct {
    uint32_t empty_mv;
    uint32_t full_mv;
} platform_battery_config_t;

typedef struct {
    uint32_t adc_mv;
    uint32_t battery_mv;
    uint8_t percent;
} platform_battery_sample_t;

/** Initialize the bounded ADC oneshot/calibration service. */
esp_err_t platform_battery_init(const platform_battery_config_t *config);

/** Take one bounded calibrated sample. The service owns the ADC resources. */
esp_err_t platform_battery_read(platform_battery_sample_t *sample);

/** Release ADC and calibration resources. Safe to call after failed init. */
esp_err_t platform_battery_deinit(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
