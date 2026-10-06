// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/radio_power_control.h"

typedef struct {
    uint8_t registers[256];
    uint8_t written_registers[16];
    unsigned reads;
    unsigned writes;
    unsigned fail_read;
    unsigned fail_write;
    bool corrupt_latch_readback;
} bus_t;

static bool read_reg(void *context, uint8_t reg, uint8_t *value)
{
    bus_t *bus = context;
    assert(reg == 0x03 || reg == 0x05 || reg == 0x07);
    if (++bus->reads == bus->fail_read) return false;
    *value = bus->registers[reg];
    if (bus->corrupt_latch_readback && bus->writes >= 3 && reg == 0x05) {
        *value ^= 1U;
    }
    return true;
}
static bool write_reg(void *context, uint8_t reg, uint8_t value)
{
    bus_t *bus = context;
    assert(reg == 0x03 || reg == 0x05 || reg == 0x07);
    assert(bus->writes < 16);
    bus->written_registers[bus->writes++] = reg;
    if (bus->writes == bus->fail_write) return false;
    bus->registers[reg] = value;
    return true;
}
static bus_t make_bus(void)
{
    bus_t bus = {0};
    /* Distinct non-RADIO state, including radio/charging/power-off pins. */
    bus.registers[0x03] = 0xa5;
    bus.registers[0x05] = 0x72;
    bus.registers[0x07] = 0xc9;
    return bus;
}
static void unchanged_other_pins(const bus_t *bus, const bus_t *before)
{
    for (unsigned reg = 0; reg < 256; ++reg) {
        uint8_t mask = (reg == 0x03 || reg == 0x05 || reg == 0x07) ? 0xfe : 0xff;
        assert((bus->registers[reg] & mask) == (before->registers[reg] & mask));
    }
}
int main(void)
{
    for (unsigned initial = 0; initial < 256; ++initial) {
        bus_t bus = make_bus();
        bus.registers[0x05] = (uint8_t)initial;
        bus_t before = bus;
        assert(tab5_radio_power_apply(&bus, read_reg, write_reg, false) == TAB5_RADIO_POWER_OK);
        assert((bus.registers[0x05] & 1U) == 0);
        assert((bus.registers[0x03] & 1U) != 0);
        assert((bus.registers[0x07] & 1U) == 0);
        assert(bus.written_registers[0] == 0x05);
        unchanged_other_pins(&bus, &before);
        assert(tab5_radio_power_apply(&bus, read_reg, write_reg, true) == TAB5_RADIO_POWER_OK);
        assert((bus.registers[0x05] & 1U) != 0);
        unchanged_other_pins(&bus, &before);
        assert(tab5_radio_power_apply(&bus, read_reg, write_reg, false) == TAB5_RADIO_POWER_OK);
        assert((bus.registers[0x05] & 1U) == 0);
    }
    for (unsigned failure = 1; failure <= 3; ++failure) {
        bus_t bus = make_bus(), before = bus;
        bus.fail_write = failure;
        assert(tab5_radio_power_apply(&bus, read_reg, write_reg, true) == TAB5_RADIO_POWER_IO_ERROR);
        assert((bus.registers[0x05] & 1U) == 0);
        unchanged_other_pins(&bus, &before);
    }
    for (unsigned failure = 1; failure <= 6; ++failure) {
        bus_t bus = make_bus(), before = bus;
        bus.fail_read = failure;
        assert(tab5_radio_power_apply(&bus, read_reg, write_reg, true) == TAB5_RADIO_POWER_IO_ERROR);
        if (failure <= 3) assert(bus.writes == 0);
        assert((bus.registers[0x05] & 1U) == 0);
        unchanged_other_pins(&bus, &before);
    }
    bus_t bus = make_bus();
    bus.corrupt_latch_readback = true;
    assert(tab5_radio_power_apply(&bus, read_reg, write_reg, true) == TAB5_RADIO_POWER_VERIFY_ERROR);
    assert((bus.registers[0x05] & 1U) == 0);
    assert(tab5_radio_power_apply(&bus, NULL, write_reg, false) == TAB5_RADIO_POWER_INVALID_ARGUMENT);
    puts("Tab5 RADIO power preserves other pins and rejects write/readback failures");
    return 0;
}
