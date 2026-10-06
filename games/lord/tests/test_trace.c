// SPDX-License-Identifier: LicenseRef-LORD-Permission
/* The benchmark's active tape must exercise real play, and changing video
 * negotiation must not change any command, RNG, progression or saved byte. */
#include "lord_internal.h"
#include "p4/game.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const p4_game_descriptor_t p4_lord_game;

int main(void)
{
    FILE *tape = fopen(LORD_TRACE_PATH, "r");
    assert(tape != NULL);
    lord_state_t state[2];
    p4_game_instance_t game[2] = {0};
    p4_game_surface_t surface[2];
    for (unsigned i = 0; i < 2; ++i) {
        const p4_game_services_t svc = {
            .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
                (i != 0 ? P4_GAME_CAP_VIDEO_HIGH_RES : 0U),
        };
        assert(p4_game_instance_start(&game[i], &p4_lord_game, &svc,
                                      &state[i], sizeof(state[i])));
        const uint16_t w = i == 0 ? 320 : 768, h = i == 0 ? 200 : 480;
        surface[i] = (p4_game_surface_t){calloc((size_t)w * h, 2), w, w, h};
        assert(surface[i].pixels != NULL);
    }
    unsigned next_frame, buttons;
    int x, y;
    int fields = fscanf(tape, "%u %u %d %d", &next_frame, &buttons, &x, &y);
    uint32_t held = 0;
    p4_game_input_t in = {0};
    unsigned counts[LORD_SCREEN_GUILD_STANDINGS + 1] = {0};
    unsigned transitions = 0, previous_screen = LORD_SCREEN_TITLE;
    for (unsigned frame = 0; frame < 2000; ++frame) {
        if (fields == 4 && frame == next_frame) {
            in.held = buttons;
            in.touch_valid = true;
            in.touch_count = (uint8_t)(x >= 0 && y >= 0 ? 1 : 0);
            if (in.touch_count != 0)
                in.touches[0] = (p4_game_point_t){(uint16_t)x, (uint16_t)y};
            fields = fscanf(tape, "%u %u %d %d", &next_frame, &buttons, &x, &y);
        }
        in.pressed = in.held & ~held;
        in.released = held & ~in.held;
        held = in.held;
        for (unsigned i = 0; i < 2; ++i) {
            assert(p4_game_instance_update(&game[i], &in,
                frame % 3U == 2U ? 16U : 17U) == P4_GAME_CONTINUE);
            const lord_state_t before = state[i];
            assert(p4_game_instance_render(&game[i], &surface[i]));
            assert(memcmp(&before, &state[i], sizeof(before)) == 0);
        }
        assert(memcmp(&state[0], &state[1], sizeof(state[0])) == 0);
        ++counts[state[0].screen];
        if (previous_screen != (unsigned)state[0].screen) {
            ++transitions;
            previous_screen = (unsigned)state[0].screen;
        }
    }
    assert(fclose(tape) == 0);
    const lord_screen_t required[] = {LORD_SCREEN_TEXT_EDITOR, LORD_SCREEN_TOWN,
        LORD_SCREEN_STATS, LORD_SCREEN_WEAPON_SHOP, LORD_SCREEN_ARMOR_SHOP,
        LORD_SCREEN_FOREST, LORD_SCREEN_BATTLE, LORD_SCREEN_MESSAGE};
    for (unsigned i = 0; i <= LORD_SCREEN_GUILD_STANDINGS; ++i)
        if (counts[i] != 0) printf("screen %u: %u frames\n", i, counts[i]);
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i)
        assert(counts[required[i]] > 0);
    assert(counts[LORD_SCREEN_BATTLE] >= 300);
    assert(transitions > 20);
    uint8_t saves[2][LORD_SAVE_MAX_BYTES];
    const size_t n = lord_save_encode(&state[0], saves[0], sizeof(saves[0]));
    assert(n > 0 && lord_save_encode(&state[1], saves[1], sizeof(saves[1])) == n);
    assert(memcmp(saves[0], saves[1], n) == 0);
    for (unsigned i = 0; i < 2; ++i) {
        in = (p4_game_input_t){.held = P4_BUTTON_BACK, .pressed = P4_BUTTON_BACK};
        assert(p4_game_instance_update(&game[i], &in, 16) == P4_GAME_EXIT_TO_LAUNCHER);
        p4_game_instance_stop(&game[i]);
        free(surface[i].pixels);
    }
    printf("%u transitions; 2000 paired frames and final saves are identical\n", transitions);
    return 0;
}
