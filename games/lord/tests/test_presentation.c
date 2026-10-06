// SPDX-License-Identifier: LicenseRef-LORD-Permission
#include "lord_internal.h"
#include "p4/game.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const p4_game_descriptor_t p4_lord_game;

static void capture(const p4_game_surface_t *s, unsigned screen, unsigned variant)
{
    const char *dir = getenv("LORD_PRESENTATION_CAPTURE_DIR");
    if (dir == NULL) return;
    char path[512];
    const int n = snprintf(path, sizeof(path), "%s/%u-%02u-%u.ppm", dir,
                           s->width, screen, variant);
    assert(n > 0 && (size_t)n < sizeof(path));
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    fprintf(f, "P6\n%u %u\n255\n", s->width, s->height);
    for (unsigned y = 0; y < s->height; ++y)
        for (unsigned x = 0; x < s->width; ++x) {
            const uint16_t p = s->pixels[y * s->stride_pixels + x];
            const unsigned char rgb[] = {
                (unsigned char)(((p >> 11U) * 255U) / 31U),
                (unsigned char)((((p >> 5U) & 63U) * 255U) / 63U),
                (unsigned char)(((p & 31U) * 255U) / 31U),
            };
            assert(fwrite(rgb, 1, 3, f) == 3);
        }
    assert(fclose(f) == 0);
}

static void render_all(unsigned width, unsigned height)
{
    const size_t stride = width + 13U, guard = 31U;
    const size_t count = stride * height + guard * 2U;
    uint16_t *memory = malloc(count * sizeof(*memory));
    assert(memory != NULL);
    p4_game_surface_t surface = { memory + guard, stride,
                                 (uint16_t)width, (uint16_t)height };
    lord_state_t state;
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
            (width == 768U ? P4_GAME_CAP_VIDEO_HIGH_RES : 0U),
    };
    p4_game_instance_t instance = {0};
    assert(p4_game_instance_start(&instance, &p4_lord_game, &services,
                                  &state, sizeof(state)));
    strcpy(state.player.name, "Rowan of the Vale");
    state.player.level = 3;
    state.player.day = 4;
    state.player.hit_points = 54;
    state.player.max_hit_points = 70;
    state.player.gold = 4321;
    state.player.bank = 9876;
    state.player.experience = 1120;
    state.player.weapon = 4;
    state.player.armor = 4;
    state.player.hero_class = LORD_CLASS_MYSTICAL;
    state.battle_kind = LORD_BATTLE_DRAGON;
    strcpy(state.enemy.name, "The Red Dragon");
    strcpy(state.enemy.weapon, "Ancient flame");
    state.enemy.hit_points = 240;
    state.enemy.max_hit_points = 320;
    strcpy(state.battle_line, "The dragon circles. Choose your next move.");
    strcpy(state.message_line_1, "Your courage has carried the day.");
    strcpy(state.message_line_2, "The town welcomes you home.");
    for (unsigned variant = 0; variant < 3; ++variant) {
        if (variant != 0) {
            state.player.gold = UINT32_MAX;
            state.player.bank = UINT32_MAX;
            state.player.experience = UINT32_MAX;
            state.player.hit_points = LORD_COMBAT_STAT_MAX;
            state.player.max_hit_points = LORD_COMBAT_STAT_MAX;
            state.enemy.hit_points = LORD_COMBAT_STAT_MAX;
            state.enemy.max_hit_points = LORD_COMBAT_STAT_MAX;
        }
        for (unsigned screen = LORD_SCREEN_TITLE;
             screen <= LORD_SCREEN_GUILD_STANDINGS; ++screen) {
            state.screen = (lord_screen_t)screen;
            state.selection = 0;
            state.menu_scroll = 0;
            if (variant == 2) lord_move_selection(&state, -1);
            state.creature_animation_ms = variant * 400U;
            for (size_t i = 0; i < count; ++i) memory[i] = 0x5aa5;
            const lord_state_t before = state;
            uint8_t save_before[LORD_SAVE_MAX_BYTES];
            uint8_t save_after[LORD_SAVE_MAX_BYTES];
            const size_t bytes = lord_save_encode(&state, save_before,
                                                  sizeof(save_before));
            assert(bytes > 0);
            assert(p4_game_instance_render(&instance, &surface));
            assert(memcmp(&before, &state, sizeof(state)) == 0);
            assert(lord_save_encode(&state, save_after, sizeof(save_after)) == bytes);
            assert(memcmp(save_before, save_after, bytes) == 0);
            for (size_t i = 0; i < guard; ++i) {
                assert(memory[i] == 0x5aa5);
                assert(memory[count - 1U - i] == 0x5aa5);
            }
            for (unsigned y = 0; y < height; ++y)
                for (size_t x = width; x < stride; ++x)
                    assert(surface.pixels[y * stride + x] == 0x5aa5);
            /* Native footer and far-right panel must be rendered, not a
             * legacy image marooned in the top-left of a large buffer. */
            if (width == 768U) {
                assert(surface.pixels[424U * stride + 700U] != 0x0842);
                assert(surface.pixels[440U * stride + 740U] == 0x10a4);
            }
            capture(&surface, screen, variant);
        }
    }
    p4_game_instance_stop(&instance);
    free(memory);
}

int main(void)
{
    assert((p4_lord_game.optional_capabilities & P4_GAME_CAP_VIDEO_HIGH_RES) != 0U);
    assert(strcmp(p4_lord_game.id, "org.p4console.lord") == 0);
    assert(p4_lord_game.launcher_id == 112U);
    assert(LORD_SAVE_FORMAT_VERSION == 5);
    render_all(768, 480);
    render_all(320, 200);
    puts("Red Dragon: 43 screens x 3 normal/extreme/scrolled variants x 2 guarded surfaces passed");
    return 0;
}
