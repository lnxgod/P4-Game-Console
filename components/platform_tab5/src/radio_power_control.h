// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef bool (*tab5_radio_read_reg_fn)(void *context, uint8_t reg, uint8_t *value);
typedef bool (*tab5_radio_write_reg_fn)(void *context, uint8_t reg, uint8_t value);

typedef enum {
    TAB5_RADIO_POWER_OK = 0,
    TAB5_RADIO_POWER_INVALID_ARGUMENT,
    TAB5_RADIO_POWER_IO_ERROR,
    TAB5_RADIO_POWER_VERIFY_ERROR,
} tab5_radio_power_result_t;

/* Caller serializes access to expander 0x44. Never resets the expander or
 * changes pins other than P0; an enable failure attempts to clear P0. */
tab5_radio_power_result_t tab5_radio_power_apply(
    void *context, tab5_radio_read_reg_fn read_reg,
    tab5_radio_write_reg_fn write_reg, bool enabled);
