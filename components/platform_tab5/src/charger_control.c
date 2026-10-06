// SPDX-License-Identifier: MIT
#include "charger_control.h"
#include <stddef.h>

enum { CHG_EN = 0x80, QC_DISABLE = 0x20, CHG_STATUS = 0x40 };
static void disable_after_failure(void *ctx, tab5_charger_read_fn read_reg,
                                  tab5_charger_write_fn write_reg)
{
    uint8_t latch;
    if (read_reg(ctx, 0x05, &latch))
        (void)write_reg(ctx, 0x05, (uint8_t)((latch | QC_DISABLE) & ~CHG_EN));
}
static tab5_charger_result_t verify(void *ctx, tab5_charger_read_fn read_reg,
                                   const uint8_t *regs, const uint8_t *expected)
{
    for (size_t i = 0; i < 4; ++i) {
        uint8_t actual;
        if (!read_reg(ctx, regs[i], &actual)) return TAB5_CHARGER_IO_ERROR;
        if (actual != expected[i]) return TAB5_CHARGER_VERIFY_ERROR;
    }
    return TAB5_CHARGER_OK;
}
tab5_charger_result_t tab5_charger_enable_500ma(
    void *ctx, tab5_charger_read_fn read_reg, tab5_charger_write_fn write_reg)
{
    if (!read_reg || !write_reg) return TAB5_CHARGER_INVALID_ARGUMENT;
    /* Latch first; then direction, high impedance and pull enable. Never
     * construct/reset this expander: USB-A, radio and shutdown share it. */
    const uint8_t regs[4] = {0x05, 0x03, 0x07, 0x0b};
    uint8_t expected[4];
    for (size_t i = 0; i < 4; ++i) {
        if (!read_reg(ctx, regs[i], &expected[i])) return TAB5_CHARGER_IO_ERROR;
    }
    expected[0] = (uint8_t)((expected[0] | QC_DISABLE) & ~CHG_EN);
    expected[1] = (uint8_t)((expected[1] | CHG_EN | QC_DISABLE) & ~CHG_STATUS);
    /* Release status as input before changing output drive; never drive P6. */
    expected[2] &= (uint8_t)~(CHG_EN | QC_DISABLE);
    expected[3] &= (uint8_t)~(CHG_EN | QC_DISABLE | CHG_STATUS);
    tab5_charger_result_t result = TAB5_CHARGER_OK;
    for (size_t i = 0; i < 4; ++i) {
        if (!write_reg(ctx, regs[i], expected[i])) {
            result = TAB5_CHARGER_IO_ERROR;
            break;
        }
    }
    if (result == TAB5_CHARGER_OK) result = verify(ctx, read_reg, regs, expected);
    /* Enable only after the disabled, non-QC configuration has read back. */
    if (result == TAB5_CHARGER_OK) {
        expected[0] |= CHG_EN;
        if (!write_reg(ctx, 0x05, expected[0])) result = TAB5_CHARGER_IO_ERROR;
        else result = verify(ctx, read_reg, regs, expected);
    }
    if (result != TAB5_CHARGER_OK) disable_after_failure(ctx, read_reg, write_reg);
    return result;
}
