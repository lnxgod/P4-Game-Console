// SPDX-License-Identifier: MIT

#ifndef P4_CALCULATOR_INTERNAL_H
#define P4_CALCULATOR_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    P4_CALC_NONE = 0,
    P4_CALC_ADD,
    P4_CALC_SUBTRACT,
    P4_CALC_MULTIPLY,
    P4_CALC_DIVIDE,
} p4_calculator_operation_t;

typedef struct {
    int32_t accumulator;
    int32_t entry;
    uint32_t held_buttons;
    p4_calculator_operation_t pending;
    uint8_t cursor;
    bool entering;
    bool error;
    bool touch_down;
} p4_calculator_state_t;

void p4_calculator_reset(p4_calculator_state_t *state);
bool p4_calculator_digit(p4_calculator_state_t *state, uint8_t digit);
bool p4_calculator_choose_operation(
    p4_calculator_state_t *state, p4_calculator_operation_t operation);
bool p4_calculator_equals(p4_calculator_state_t *state);
void p4_calculator_backspace(p4_calculator_state_t *state);
size_t p4_calculator_format(
    const p4_calculator_state_t *state, char *output, size_t output_bytes);

#endif
