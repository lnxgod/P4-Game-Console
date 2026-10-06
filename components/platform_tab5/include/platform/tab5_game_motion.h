// SPDX-License-Identifier: MIT
#pragma once
#include <string.h>
#include "platform/tab5_sensors.h"
#include "p4/motion.h"
/* Pure adapter: separate IMU timestamp prevents RTC/power updates refreshing
 * old motion. Physical axes are intentionally preserved for pose calibration. */
static inline bool platform_tab5_game_motion(const platform_tab5_telemetry_t *s,
    uint64_t now_us, p4_game_motion_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!s || !s->imu_valid || !s->motion_sampled_us || !s->motion_sequence ||
        now_us < s->motion_sampled_us || now_us - s->motion_sampled_us > 150000U)
        return false;
    out->valid = true;
    out->sequence = s->motion_sequence;
    out->age_ms = (uint16_t)((now_us - s->motion_sampled_us) / 1000U);
    memcpy(out->accel_mg, s->accel_mg, sizeof(out->accel_mg));
    memcpy(out->gyro_mdps, s->gyro_mdps, sizeof(out->gyro_mdps));
    return true;
}
