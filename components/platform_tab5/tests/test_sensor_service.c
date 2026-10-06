// SPDX-License-Identifier: MIT
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <string.h>
#include "../src/sensors.c"

static jmp_buf finished;
static unsigned steps, devices, identities, power_reads, charger_calls;
static int64_t now_us = 1000000;
static int fake_bus, fake_power;
const char *esp_err_to_name(int value) { (void)value; return "mock"; }
void mock_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
i2c_master_bus_handle_t platform_tab5_i2c(void) { return &fake_bus; }
esp_err_t platform_tab5_charger_init(void)
{ return ++charger_calls == 1 ? ESP_ERR_TIMEOUT : ESP_OK; }
int64_t esp_timer_get_time(void) { return now_us; }
int xTaskCreate(void (*entry)(void *), const char *name, unsigned stack, void *arg,
                unsigned priority, TaskHandle_t *handle)
{
    (void)entry; (void)name; (void)stack; (void)arg; (void)priority;
    *handle = &fake_bus; return pdPASS;
}
esp_err_t platform_tab5_rtc_write(i2c_master_dev_handle_t dev, uint32_t seconds)
{ (void)dev; (void)seconds; return ESP_FAIL; }
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
    const i2c_device_config_t *config, i2c_master_dev_handle_t *out)
{
    assert(bus == &fake_bus);
    if (config->device_address != 0x41) return ESP_FAIL;
    ++devices; *out = &fake_power; return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev,
    const uint8_t *bytes, size_t count, int timeout)
{
    (void)timeout; assert(dev == &fake_power);
    assert(count == 3 && bytes[0] == 0 && bytes[1] == 0x45 && bytes[2] == 0x27);
    return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev,
    const uint8_t *reg, size_t writes, uint8_t *out, size_t reads, int timeout)
{
    (void)timeout; assert(dev == &fake_power && writes == 1 && reads == 2);
    uint16_t value;
    switch (*reg) {
    case 0xfe: if (++identities == 1) return ESP_ERR_TIMEOUT; value = 0x5449; break;
    case 0xff: value = 0x2260; break;
    case 0: value = 0x4527; break;
    case 2:
        ++power_reads;
        if (power_reads == 2) return ESP_ERR_TIMEOUT;
        value = power_reads == 3 ? 920 : 5920; /* Invalid 1.15 V, then 7.4 V. */
        break;
    case 1: value = 200; break;
    default: assert(false); return ESP_FAIL;
    }
    out[0] = (uint8_t)(value >> 8); out[1] = (uint8_t)value;
    return ESP_OK;
}
void vTaskDelay(unsigned ticks)
{
    assert(ticks == 20);
    ++steps;
    const platform_tab5_telemetry_t sample = platform_tab5_sensors_snapshot();
    if (steps <= 250) assert(!sample.battery_ready && !sample.battery_valid);
    if (steps == 3011) {
        assert(sample.battery_ready && sample.battery_valid);
        assert(sample.battery_mv == 7400 && sample.battery_percent == 50);
        assert(devices == 1); /* Retry never leaks/duplicates an I2C handle. */
    }
    if (steps == 301) assert(!sample.battery_valid && sample.battery_error == ESP_ERR_TIMEOUT);
    if (steps == 351) assert(!sample.battery_valid && sample.battery_mv == 1150);
    if (steps == 401) assert(sample.battery_valid && sample.battery_percent == 50);
    now_us += 20000;
    if (steps == 440) longjmp(finished, 1);
}
int main(void)
{
    if (setjmp(finished) == 0) sensor_task(NULL);
    assert(identities == 2 && devices == 1 && power_reads == 4);
    #if defined(CONFIG_P4_TAB5_CHARGER_500MA) && CONFIG_P4_TAB5_CHARGER_500MA
    assert(charger_calls == 2);
#else
    assert(charger_calls == 0);
#endif
    assert(platform_tab5_sensors_snapshot().battery_valid);
    now_us += 2000001;
    assert(!platform_tab5_sensors_snapshot().battery_valid);
    return 0;
}
