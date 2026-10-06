// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef bool (*tab5_charger_read_fn)(void *, uint8_t, uint8_t *);
typedef bool (*tab5_charger_write_fn)(void *, uint8_t, uint8_t);
typedef enum {
    TAB5_CHARGER_OK = 0,
    TAB5_CHARGER_IO_ERROR,
    TAB5_CHARGER_VERIFY_ERROR,
    TAB5_CHARGER_INVALID_ARGUMENT,
} tab5_charger_result_t;

/* C145 IP2326: expander 0x44 P7=CHG_EN, P5=nCHG_QC_EN,
 * P6=CHG_STAT input. Standard 500 mA selection from pinned M5Unified.
 * All other expander bits are preserved; caller owns the shared bus lock.
 * Readback proves control configuration, not actual battery charging. */
tab5_charger_result_t tab5_charger_enable_500ma(
    void *context, tab5_charger_read_fn read_reg, tab5_charger_write_fn write_reg);
