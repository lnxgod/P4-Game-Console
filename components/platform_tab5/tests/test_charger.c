// SPDX-License-Identifier: MIT
#include "../src/charger_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t regs[256], initial[256];
    unsigned calls, fail_at, corrupt_at, writes;
    bool stuck_enable;
} fake_t;
static bool read_reg(void *ctx, uint8_t reg, uint8_t *value)
{
    fake_t *f = ctx;
    ++f->calls;
    if (f->calls == f->fail_at) return false;
    *value = f->regs[reg];
    if (f->calls == f->corrupt_at) *value ^= 1U;
    return true;
}
static bool write_reg(void *ctx, uint8_t reg, uint8_t value)
{
    fake_t *f = ctx;
    ++f->calls;
    if (f->calls == f->fail_at) return false;
    assert(reg == 0x05 || reg == 0x07 || reg == 0x0b || reg == 0x03);
    const unsigned allowed = reg == 0x05 || reg == 0x07 ? 0xa0U : 0xe0U;
    assert(((f->regs[reg] ^ value) & ~allowed) == 0U);
    /* Before enabling, the readback-safe configuration must be established. */
    if (reg == 0x05 && (value & 0x80U)) {
        assert(value & 0x20U);
        assert((f->regs[0x03] & 0xe0U) == 0xa0U);
        assert((f->regs[0x07] & 0xa0U) == 0U);
        assert((f->regs[0x0b] & 0xe0U) == 0U);
    }
    if (!(f->stuck_enable && reg == 0x05 && (value & 0x80U)))
        f->regs[reg] = value;
    ++f->writes;
    return true;
}
static fake_t fresh(unsigned seed)
{
    fake_t f = {0};
    for (unsigned i = 0; i < 256; ++i) f.regs[i] = (uint8_t)(seed ^ i);
    memcpy(f.initial, f.regs, sizeof(f.regs));
    return f;
}
static void unchanged_other_pins(const fake_t *f)
{
    for (unsigned i = 0; i < 256; ++i) {
        unsigned allowed = i == 0x05 || i == 0x07 ? 0xa0U :
            i == 0x03 || i == 0x0b ? 0xe0U : 0U;
        assert(((f->regs[i] ^ f->initial[i]) & ~allowed) == 0U);
    }
}
int main(void)
{
    assert(tab5_charger_enable_500ma(NULL, NULL, write_reg) == TAB5_CHARGER_INVALID_ARGUMENT);
    for (unsigned seed = 0; seed < 256; ++seed) {
        fake_t f = fresh(seed);
        assert(tab5_charger_enable_500ma(&f, read_reg, write_reg) == TAB5_CHARGER_OK);
        assert(f.calls == 17 && f.writes == 5);
        assert((f.regs[0x05] & 0xa0U) == 0xa0U);
        assert((f.regs[0x03] & 0xe0U) == 0xa0U);
        unchanged_other_pins(&f);
    }
    for (unsigned step = 1; step <= 17; ++step) {
        fake_t f = fresh(0x5a);
        f.fail_at = step;
        assert(tab5_charger_enable_500ma(&f, read_reg, write_reg) == TAB5_CHARGER_IO_ERROR);
        unchanged_other_pins(&f);
        if (step <= 4) assert(f.writes == 0);
        else assert((f.regs[0x05] & 0x80U) == 0);
    }
    for (unsigned step = 9; step <= 17; ++step) {
        if (step == 13) continue; /* Enable write, not a readback. */
        fake_t f = fresh(0x5a);
        f.corrupt_at = step;
        assert(tab5_charger_enable_500ma(&f, read_reg, write_reg) == TAB5_CHARGER_VERIFY_ERROR);
        assert((f.regs[0x05] & 0x80U) == 0);
        unchanged_other_pins(&f);
    }
    fake_t f = fresh(0x5a); f.stuck_enable = true;
    assert(tab5_charger_enable_500ma(&f, read_reg, write_reg) == TAB5_CHARGER_VERIFY_ERROR);
    assert((f.regs[0x05] & 0x80U) == 0);
    puts("Tab5 charger: shared-pin preservation and every transaction failure passed");
    return 0;
}
