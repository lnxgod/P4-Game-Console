// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include "p4/dice.h"
void dice_feedback_begin();
void dice_feedback_stop();
void dice_feedback_settle(uint32_t now);
bool dice_feedback_update(uint32_t now, float motion, p4_dice_phase_t phase, bool enabled);
