// SPDX-License-Identifier: MIT
#include "rtc_clock.h"
#include "platform/tab5_sensors.h"
#include <string.h>

static esp_err_t read_regs(i2c_master_dev_handle_t device, uint8_t reg,
                           uint8_t *out, size_t size)
{
    return i2c_master_transmit_receive(device, &reg, 1, out, size, 50);
}
static esp_err_t write_byte(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value)
{
    const uint8_t bytes[] = {reg, value};
    return i2c_master_transmit(device, bytes, sizeof(bytes), 50);
}
static bool matches(const uint8_t raw[16], uint32_t requested)
{
    uint8_t calendar[16];
    memcpy(calendar, raw, sizeof(calendar));
    /* Verify the new calendar before acknowledging old VLF/STOP status. */
    calendar[13] &= (uint8_t)~0x02U;
    calendar[14] &= (uint8_t)~0x40U;
    platform_tab5_datetime_t date;
    uint32_t actual = 0;
    return platform_tab5_decode_rtc(calendar, &date) &&
        platform_tab5_datetime_to_unix(&date, &actual) && actual >= requested &&
        (uint64_t)actual <= (uint64_t)requested + 2U;
}
esp_err_t platform_tab5_rtc_write(i2c_master_dev_handle_t device, uint32_t seconds)
{
    uint8_t bytes[8] = {0x10}, raw[16];
    if (!device || !platform_tab5_encode_rtc(seconds, bytes + 1)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = read_regs(device, 0x10, raw, sizeof(raw));
    if (err != ESP_OK) return err;
    if (raw[14] & 0x80U) return ESP_ERR_INVALID_STATE; /* Never replay TEST mode. */
    /* Epson ETM50E-10 section 18.5 permits a sequential write without STOP.
     * This avoids disabling backup switchover while the transfer is in flight. */
    err = i2c_master_transmit(device, bytes, sizeof(bytes), 50);
    if (err == ESP_OK) err = read_regs(device, 0x10, raw, sizeof(raw));
    if (err == ESP_OK && !matches(raw, seconds)) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) return err;
    /* Flag register is W0C: clear only VLF, preserving unrelated event flags.
     * Bit 6 is reserved zero; VBFF is read-only. */
    err = write_byte(device, 0x1d, 0xbd);
    if (err == ESP_OK && (raw[14] & 0x40U))
        err = write_byte(device, 0x1e, (uint8_t)(raw[14] & (uint8_t)~0x40U));
    if (err == ESP_OK) err = read_regs(device, 0x10, raw, sizeof(raw));
    platform_tab5_datetime_t date;
    if (err == ESP_OK && (!platform_tab5_decode_rtc(raw, &date) || !matches(raw, seconds)))
        err = ESP_ERR_INVALID_RESPONSE;
    return err;
}
