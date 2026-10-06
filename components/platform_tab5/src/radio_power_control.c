// SPDX-License-Identifier: MIT
#include "radio_power_control.h"
#include <stddef.h>

enum {
    RADIO_ENABLE = 1U << 0,
    IO_DIRECTION = 0x03,
    OUTPUT_LATCH = 0x05,
    OUTPUT_HIGH_Z = 0x07,
};

tab5_radio_power_result_t tab5_radio_power_apply(
    void *context, tab5_radio_read_reg_fn read_reg,
    tab5_radio_write_reg_fn write_reg, bool enabled)
{
    if (read_reg == NULL || write_reg == NULL) {
        return TAB5_RADIO_POWER_INVALID_ARGUMENT;
    }
    /* Unlike the vendor expander constructor, these masked operations never
     * reset charging, USB or power-off controls sharing the same device. */
    const uint8_t registers[] = {OUTPUT_LATCH, OUTPUT_HIGH_Z, IO_DIRECTION};
    uint8_t expected[3];
    for (size_t i = 0; i < 3; ++i) {
        if (!read_reg(context, registers[i], &expected[i])) {
            return TAB5_RADIO_POWER_IO_ERROR;
        }
    }
    expected[0] = enabled ? (uint8_t)(expected[0] | RADIO_ENABLE)
                          : (uint8_t)(expected[0] & (uint8_t)~RADIO_ENABLE);
    expected[1] &= (uint8_t)~RADIO_ENABLE;
    expected[2] |= RADIO_ENABLE;

    tab5_radio_power_result_t result = TAB5_RADIO_POWER_OK;
    /* Latch first, then driven output mode and direction. This makes the
     * initial disable drive low without a transient high RADIO power pulse. */
    for (size_t i = 0; i < 3; ++i) {
        if (!write_reg(context, registers[i], expected[i])) {
            result = TAB5_RADIO_POWER_IO_ERROR;
            break;
        }
    }
    for (size_t i = 0; i < 3 && result == TAB5_RADIO_POWER_OK; ++i) {
        uint8_t actual = 0;
        if (!read_reg(context, registers[i], &actual)) {
            result = TAB5_RADIO_POWER_IO_ERROR;
        } else if (actual != expected[i]) {
            result = TAB5_RADIO_POWER_VERIFY_ERROR;
        }
    }
    if (result != TAB5_RADIO_POWER_OK && enabled) {
        /* Best effort only: preserve other live latch bits and report the
         * failure even if this rollback succeeds. Never claim power is off
         * when a bus error prevented readback. */
        uint8_t actual = 0;
        if (read_reg(context, OUTPUT_LATCH, &actual)) {
            (void)write_reg(context, OUTPUT_LATCH,
                            (uint8_t)(actual & (uint8_t)~RADIO_ENABLE));
        }
    }
    return result;
}
