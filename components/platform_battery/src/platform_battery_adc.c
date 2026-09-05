// SPDX-License-Identifier: MIT

#include "platform_battery/battery.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "soc/adc_channel.h"

#if ADC1_GPIO20_CHANNEL != PLATFORM_BATTERY_ADC1_CHANNEL
#error "Pinned ESP32-P4 ADC map no longer maps GPIO20 to ADC1 channel 4"
#endif

enum { PLATFORM_BATTERY_SAMPLE_COUNT = 8U };
static const char *const TAG = "platform_battery";
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static platform_battery_config_t s_config;
static bool s_initialized;
static bool s_calibrated;

esp_err_t platform_battery_init(const platform_battery_config_t *config)
{
    const platform_battery_config_t defaults = {
        .empty_mv = PLATFORM_BATTERY_DEFAULT_EMPTY_MV,
        .full_mv = PLATFORM_BATTERY_DEFAULT_FULL_MV,
    };
    if (config == NULL) config = &defaults;
    if (config->full_mv <= config->empty_mv) return ESP_ERR_INVALID_ARG;
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    adc_unit_t unit;
    adc_channel_t channel;
    const esp_err_t map_result = adc_oneshot_io_to_channel(
        (gpio_num_t)PLATFORM_BATTERY_ADC_GPIO, &unit, &channel);
    if (map_result != ESP_OK || unit != ADC_UNIT_1 ||
        channel != (adc_channel_t)PLATFORM_BATTERY_ADC1_CHANNEL) {
        ESP_LOGE(TAG, "GPIO%u is not ADC-capable in this IDF target",
                 PLATFORM_BATTERY_ADC_GPIO);
        return ESP_ERR_NOT_SUPPORTED;
    }
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = unit,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t result = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (result != ESP_OK) {
        return result;
    }
    const adc_oneshot_chan_cfg_t channel_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    result = adc_oneshot_config_channel(s_adc, channel, &channel_cfg);
    if (result != ESP_OK) {
        (void)adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
        return result;
    }
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit, .chan = channel, .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    result = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = unit, .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    result = adc_cali_create_scheme_line_fitting(&cali_cfg, &s_cali);
#else
    result = ESP_ERR_NOT_SUPPORTED;
#endif
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration unavailable: %s", esp_err_to_name(result));
        (void)adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
        return result;
    }
    s_config = *config;
    s_calibrated = true;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t platform_battery_read(platform_battery_sample_t *sample)
{
    if (sample == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_initialized || !s_calibrated) return ESP_ERR_INVALID_STATE;
    adc_unit_t unit;
    adc_channel_t channel;
    esp_err_t result = adc_oneshot_io_to_channel(
        (gpio_num_t)PLATFORM_BATTERY_ADC_GPIO, &unit, &channel);
    if (result != ESP_OK || unit != ADC_UNIT_1 ||
        channel != (adc_channel_t)PLATFORM_BATTERY_ADC1_CHANNEL) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    uint32_t total = 0U;
    for (unsigned index = 0U; index < PLATFORM_BATTERY_SAMPLE_COUNT; ++index) {
        int raw;
        result = adc_oneshot_read(s_adc, channel, &raw);
        if (result != ESP_OK) return result;
        int mv;
        result = adc_cali_raw_to_voltage(s_cali, raw, &mv);
        if (result != ESP_OK || mv < 0) {
            return result == ESP_OK ? ESP_ERR_INVALID_RESPONSE : result;
        }
        total += (uint32_t)mv;
    }
    sample->adc_mv = (total + PLATFORM_BATTERY_SAMPLE_COUNT / 2U) /
        PLATFORM_BATTERY_SAMPLE_COUNT;
    sample->battery_mv = platform_battery_adc_mv_to_battery_mv(sample->adc_mv);
    sample->percent = platform_battery_voltage_to_percent(
        sample->battery_mv, s_config.empty_mv, s_config.full_mv);
    return ESP_OK;
}

esp_err_t platform_battery_deinit(void)
{
    if (!s_initialized) return ESP_OK;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    esp_err_t result = adc_cali_delete_scheme_curve_fitting(s_cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    esp_err_t result = adc_cali_delete_scheme_line_fitting(s_cali);
#else
    esp_err_t result = ESP_ERR_NOT_SUPPORTED;
#endif
    if (result != ESP_OK) return result;
    result = adc_oneshot_del_unit(s_adc);
    if (result == ESP_OK) {
        s_adc = NULL; s_cali = NULL; s_initialized = false; s_calibrated = false;
    }
    return result;
}
