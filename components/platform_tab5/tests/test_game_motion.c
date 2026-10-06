// SPDX-License-Identifier: MIT
#include <assert.h>
#include "platform/tab5_game_motion.h"
int main(void)
{
    platform_tab5_telemetry_t s={.sampled_us=3000000,.motion_sampled_us=1000000,
        .motion_sequence=31,.imu_valid=true,.accel_mg={100,-200,980},
        .gyro_mdps={-10000,25000,34000}};
    p4_game_motion_t out;
    assert(platform_tab5_game_motion(&s,1150000,&out));
    assert(out.age_ms==150 && out.sequence==31 && out.accel_mg[0]==100 &&
        out.accel_mg[1]==-200 && out.accel_mg[2]==980 &&
        out.gyro_mdps[0]==-10000 && out.gyro_mdps[1]==25000 && out.gyro_mdps[2]==34000);
    assert(!platform_tab5_game_motion(&s,1150001,&out) && !out.valid && !out.sequence);
    assert(!platform_tab5_game_motion(&s,999999,&out));
    s.imu_valid=false;assert(!platform_tab5_game_motion(&s,1010000,&out));
    s.imu_valid=true;s.motion_sequence=0;assert(!platform_tab5_game_motion(&s,1010000,&out));
    assert(!platform_tab5_game_motion(NULL,1010000,&out));
    assert(!platform_tab5_game_motion(&s,1010000,NULL));
    return 0;
}
