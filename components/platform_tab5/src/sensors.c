// SPDX-License-Identifier: MIT
// Register references and Bosch configuration provenance: third_party/tab5-sensors.json.
#include "platform/tab5_sensors.h"
#include "platform/tab5.h"
#include "rtc_clock.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "../../../third_party/bmi270/config.inc"

static i2c_master_dev_handle_t s_power, s_imu, s_rtc;
static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static platform_tab5_telemetry_t s_snapshot;
static TaskHandle_t s_task;
static bool s_clock_pending;
static uint32_t s_clock_requested;
static int s_clock_result;
static const char *TAG = "tab5_sensors";

static esp_err_t device(uint16_t address, i2c_master_dev_handle_t *out)
{
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = address,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(platform_tab5_i2c(), &config, out);
}
static esp_err_t read_regs(i2c_master_dev_handle_t dev, uint8_t reg, void *out, size_t count)
{
    return i2c_master_transmit_receive(dev, &reg, 1, out, count, 50);
}
static esp_err_t write_regs(i2c_master_dev_handle_t dev, uint8_t reg, const uint8_t *data, size_t count)
{
    if (!data || !count || count > 32) return ESP_ERR_INVALID_ARG;
    uint8_t bytes[33]; bytes[0] = reg; memcpy(bytes + 1, data, count);
    return i2c_master_transmit(dev, bytes, count + 1, 50);
}
static esp_err_t write_byte(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    return write_regs(dev, reg, &value, 1);
}
static uint16_t be16(const uint8_t bytes[2])
{
    return (uint16_t)((uint16_t)bytes[0] << 8 | bytes[1]);
}
static esp_err_t init_power(void)
{
    esp_err_t err = device(0x41, &s_power);
    uint8_t manufacturer[2] = {0}, chip[2] = {0};
    if (err == ESP_OK) err = read_regs(s_power, 0xfe, manufacturer, 2);
    if (err == ESP_OK) err = read_regs(s_power, 0xff, chip, 2);
    if (err == ESP_OK && (be16(manufacturer) != 0x5449 || (be16(chip) & 0xfff0) != 0x2260))
        err = ESP_ERR_INVALID_RESPONSE;
    /* 16 samples, 1.1 ms bus/shunt conversion, continuous both. Current is
     * derived directly from shunt voltage, avoiding a calibration-register dependency. */
    const uint8_t config[] = {0x45, 0x27};
    uint8_t observed[2] = {0};
    if (err == ESP_OK) err = write_regs(s_power, 0, config, sizeof(config));
    if (err == ESP_OK) err = read_regs(s_power, 0, observed, 2);
    if (err == ESP_OK && be16(observed) != 0x4527) err = ESP_ERR_INVALID_RESPONSE;
    return err;
}
static esp_err_t init_imu(void)
{
    esp_err_t err = device(0x68, &s_imu);
    uint8_t id = 0;
    if (err == ESP_OK) err = read_regs(s_imu, 0, &id, 1);
    if (err == ESP_OK && id != 0x24) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) return err;
    err = write_byte(s_imu, 0x7e, 0xb6);
    vTaskDelay(pdMS_TO_TICKS(10));
    if (err == ESP_OK) err = write_byte(s_imu, 0x7c, 0); /* Advanced power save off. */
    vTaskDelay(pdMS_TO_TICKS(10));
    if (err == ESP_OK) err = write_byte(s_imu, 0x59, 0);
    for (size_t offset = 0; err == ESP_OK && offset < sizeof(bmi270_config_file); offset += 32) {
        const uint8_t address[] = {(uint8_t)((offset / 2) & 15), (uint8_t)(offset / 32)};
        err = write_regs(s_imu, 0x5b, address, sizeof(address));
        if (err == ESP_OK) err = write_regs(s_imu, 0x5e, bmi270_config_file + offset, 32);
        if ((offset & 255) == 0) vTaskDelay(1); /* Let touch/UI work during upload. */
    }
    if (err == ESP_OK) err = write_byte(s_imu, 0x59, 1);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(150));
    uint8_t status = 0;
    err = read_regs(s_imu, 0x21, &status, 1);
    if (err == ESP_OK && (status & 15U) != 1U) err = ESP_ERR_INVALID_RESPONSE;
    /* Normal filtering, 100 Hz, +/-4g, +/-2000 deg/s. No auxiliary sensor,
     * interrupt mapping, wake-up, or physical display rotation changes. */
    const uint8_t config[] = {0xa8, 1, 0xe8, 0};
    uint8_t observed[sizeof(config)] = {0};
    if (err == ESP_OK) err = write_regs(s_imu, 0x40, config, sizeof(config));
    if (err == ESP_OK) err = read_regs(s_imu, 0x40, observed, sizeof(observed));
    if (err == ESP_OK && memcmp(config, observed, sizeof(config))) err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK) err = write_byte(s_imu, 0x7d, 0x0e);
    vTaskDelay(pdMS_TO_TICKS(100));
    return err;
}
static esp_err_t init_rtc(void)
{
    esp_err_t err = device(0x32, &s_rtc);
    uint8_t ctrl = 0;
    if (err == ESP_OK) err = read_regs(s_rtc, 0x1f, &ctrl, 1);
    /* M5's fitted rechargeable backup supply: enable backup switchover and
     * charging, preserving other bits. Never clear VLF or invent a date. */
    if (err == ESP_OK) err = write_byte(s_rtc, 0x1f, (uint8_t)(ctrl | 0x30));
    if (err == ESP_OK) err = read_regs(s_rtc, 0x1f, &ctrl, 1);
    if (err == ESP_OK && (ctrl & 0x30) != 0x30) err = ESP_ERR_INVALID_RESPONSE;
    return err;
}
static void publish(const platform_tab5_telemetry_t *sample)
{
    portENTER_CRITICAL(&s_snapshot_lock); s_snapshot = *sample; portEXIT_CRITICAL(&s_snapshot_lock);
}
static void sample_power(platform_tab5_telemetry_t *sample)
{
    uint8_t bus[2], shunt[2];
    esp_err_t err = read_regs(s_power, 2, bus, 2);
    if (err == ESP_OK) err = read_regs(s_power, 1, shunt, 2);
    if (err == ESP_OK && !platform_tab5_decode_power(be16(bus), be16(shunt),
            &sample->battery_mv, &sample->battery_ma, &sample->battery_percent))
        err = ESP_ERR_INVALID_RESPONSE;
    sample->battery_error = err; sample->battery_valid = err == ESP_OK;
}
static void sample_motion(platform_tab5_telemetry_t *sample)
{
    uint8_t data[12], temp[2], status = 0;
    esp_err_t err = read_regs(s_imu, 3, &status, 1);
    if (err == ESP_OK && (status & 0xc0) != 0xc0) err = ESP_ERR_NOT_FINISHED;
    if (err == ESP_OK) err = read_regs(s_imu, 0x0c, data, sizeof(data));
    if (err == ESP_OK) platform_tab5_decode_motion(data, sample->accel_mg, sample->gyro_mdps);
    sample->imu_error = err; sample->imu_valid = err == ESP_OK;
    sample->temperature_valid = false;
    if (err == ESP_OK && read_regs(s_imu, 0x22, temp, 2) == ESP_OK) {
        const uint16_t raw = (uint16_t)(temp[0] | (uint16_t)temp[1] << 8);
        if (raw != 0x8000) {
            int32_t signed_temp = raw <= INT16_MAX ? (int32_t)raw : (int32_t)raw - 65536;
            sample->temperature_mc = 23000 + signed_temp * 1000 / 512;
            sample->temperature_valid = true;
        }
    }
}
static void sample_clock(platform_tab5_telemetry_t *sample)
{
    uint8_t data[16];
    esp_err_t err = read_regs(s_rtc, 0x10, data, sizeof(data));
    if (err == ESP_OK && !platform_tab5_decode_rtc(data, &sample->rtc)) err = ESP_ERR_INVALID_RESPONSE;
    sample->rtc_error = err; sample->rtc_valid = err == ESP_OK;
}
static void sensor_task(void *unused)
{
    (void)unused;
    platform_tab5_telemetry_t sample = {0};
    sample.battery_error = init_power(); sample.battery_ready = sample.battery_error == ESP_OK;
    sample.rtc_error = init_rtc(); sample.rtc_present = sample.rtc_error == ESP_OK;
    sample.imu_error = init_imu(); sample.imu_ready = sample.imu_error == ESP_OK;
    ESP_LOGI(TAG, "SENSORS_INIT ina226=%s bmi270=%s rx8130=%s",
        esp_err_to_name(sample.battery_error), esp_err_to_name(sample.imu_error), esp_err_to_name(sample.rtc_error));
    bool first = true; uint8_t last_percent = 255; unsigned tick = 0;
    for (;;) {
        portENTER_CRITICAL(&s_snapshot_lock);
        const bool set_clock = s_clock_pending;
        const uint32_t requested = s_clock_requested;
        portEXIT_CRITICAL(&s_snapshot_lock);
        if (set_clock) {
            const esp_err_t result = platform_tab5_rtc_write(s_rtc, requested);
            sample_clock(&sample);
            if (result != ESP_OK) sample.rtc_valid = false;
            sample.sampled_us = (uint64_t)esp_timer_get_time();
            portENTER_CRITICAL(&s_snapshot_lock);
            s_snapshot = sample;
            s_clock_result = result;
            s_clock_pending = false;
            portEXIT_CRITICAL(&s_snapshot_lock);
            ESP_LOGI(TAG, "RTC_SET unix=%lu result=%s readback=%u",
                (unsigned long)requested, esp_err_to_name(result),
                (unsigned)(result == ESP_OK && sample.rtc_valid));
        }
        if ((tick++ % 4U) == 0U) {
            if (sample.battery_ready) sample_power(&sample);
            if (sample.rtc_present) sample_clock(&sample);
        }
        if (sample.imu_ready) sample_motion(&sample);
        /* A failed setting attempt is not accepted just because its partial
         * calendar happens to decode. A new verified sync clears this error. */
        if (s_clock_result != ESP_OK) sample.rtc_valid = false;
        sample.sampled_us = (uint64_t)esp_timer_get_time();
        publish(&sample);
        if (first || tick % 40U == 1U || (sample.battery_valid && sample.battery_percent != last_percent)) {
            ESP_LOGI(TAG, "SENSORS_SAMPLE battery_valid=%u mv=%u ma=%ld percent=%u estimate=voltage imu_valid=%u accel_mg=%ld,%ld,%ld gyro_mdps=%ld,%ld,%ld rtc_valid=%u date=%04u-%02u-%02u time=%02u:%02u:%02u",
                (unsigned)sample.battery_valid, sample.battery_mv, (long)sample.battery_ma, sample.battery_percent,
                (unsigned)sample.imu_valid, (long)sample.accel_mg[0], (long)sample.accel_mg[1], (long)sample.accel_mg[2],
                (long)sample.gyro_mdps[0], (long)sample.gyro_mdps[1], (long)sample.gyro_mdps[2],
                (unsigned)sample.rtc_valid, sample.rtc.year, sample.rtc.month, sample.rtc.day, sample.rtc.hour, sample.rtc.minute, sample.rtc.second);
            first = false; last_percent = sample.battery_percent;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
esp_err_t platform_tab5_sensors_start(void)
{
    if (s_task) return ESP_OK;
    if (!platform_tab5_i2c()) return ESP_ERR_INVALID_STATE;
    return xTaskCreate(sensor_task, "tab5_sensors", 4096, NULL, 1, &s_task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
platform_tab5_telemetry_t platform_tab5_sensors_snapshot(void)
{
    platform_tab5_telemetry_t result;
    portENTER_CRITICAL(&s_snapshot_lock);
    result = s_snapshot;
    result.rtc_set_pending = s_clock_pending;
    result.rtc_set_error = s_clock_result;
    portEXIT_CRITICAL(&s_snapshot_lock);
    if (!result.sampled_us || (uint64_t)esp_timer_get_time() - result.sampled_us > 2000000U) {
        result.battery_valid = result.imu_valid = result.rtc_valid = result.temperature_valid = false;
    }
    return result;
}

esp_err_t platform_tab5_clock_set(uint32_t unix_seconds)
{
    platform_tab5_datetime_t date;
    if (!platform_tab5_datetime_from_unix(unix_seconds, &date)) return ESP_ERR_INVALID_ARG;
    esp_err_t result = ESP_OK;
    portENTER_CRITICAL(&s_snapshot_lock);
    if (!s_snapshot.rtc_present) result = ESP_ERR_INVALID_STATE;
    else if (s_clock_pending) result = ESP_ERR_NOT_FINISHED;
    else {
        s_clock_requested = unix_seconds;
        s_clock_pending = true;
    }
    portEXIT_CRITICAL(&s_snapshot_lock);
    return result;
}
