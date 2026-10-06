// SPDX-License-Identifier: MIT
#ifndef BLAST_CIRCUIT_INTERNAL_H
#define BLAST_CIRCUIT_INTERNAL_H
#include "p4/game.h"

enum {
    BC_W = 17, BC_H = 13, BC_CELLS = BC_W * BC_H,
    BC_PLAYERS = 4, BC_BOMBS = 12, BC_TICK_MS = 50,
    BC_FUSE = 50, BC_FIRE = 8, BC_EMBER_FIRE = BC_FIRE + 20, BC_WIN_SCORE = 3,
    BC_PROTOCOL = 3, BC_SNAPSHOT = 16 + BC_PLAYERS * 9 + BC_BOMBS * 5 + BC_CELLS * 2,
    BC_CHUNK = 48, BC_PARTS = (BC_SNAPSHOT + BC_CHUNK - 1) / BC_CHUNK, BC_PACKET = 60,
    BC_EVENT_BOMB = 1, BC_EVENT_BLAST = 2, BC_EVENT_PICKUP = 4,
    BC_EVENT_WIN = 8,
    BC_THEMES = 3, BC_CUSTOM = 3, BC_LEVEL_BYTES = 1 + BC_CELLS,
    BC_LEVEL_PARTS = (BC_LEVEL_BYTES + BC_CHUNK - 1) / BC_CHUNK,
    BC_SAVE_BYTES = 5 + BC_CUSTOM * BC_LEVEL_BYTES + 4,
};
typedef enum { BC_FLOOR, BC_WALL, BC_CRATE, BC_RANGE, BC_EXTRA, BC_SPEED, BC_ARMOR, BC_DAMAGED, BC_GLASS, BC_FUEL } bc_tile_t;
typedef enum { BC_TITLE, BC_READY, BC_PLAY, BC_ROUND, BC_MATCH,
               BC_PAUSED, BC_WAIT, BC_LOST, BC_SELECT, BC_EDITOR, BC_EDITOR_MENU, BC_TRANSFER, BC_COPY } bc_phase_t;
typedef enum {
    BC_S_NAV, BC_S_CONFIRM, BC_S_PLACE, BC_S_BLAST, BC_S_WOOD, BC_S_METAL,
    BC_S_GLASS, BC_S_RANGE, BC_S_EXTRA, BC_S_SPEED, BC_S_STEP, BC_S_DEATH,
    BC_S_COUNT, BC_S_GO, BC_S_ROUND, BC_S_CHAMPION, BC_S_DRAW, BC_S_PAUSE,
    BC_S_PAINT, BC_S_ERASE, BC_S_SAVE, BC_S_ERROR, BC_S_TRANSFER, BC_S_WARNING,
    BC_S_STEP_METAL, BC_S_STEP_GLASS, BC_S_DEATH_EMBER, BC_S_DEATH_VOLT, BC_S_DEATH_GILT,
    BC_S_SIZZLE, BC_SOUNDS
} bc_sound_t;
typedef struct { uint8_t theme, tile[BC_CELLS]; } bc_level_t;
typedef enum { BC_LEVEL_OK, BC_LEVEL_BAD_TILE, BC_LEVEL_SPAWN, BC_LEVEL_DISCONNECTED, BC_LEVEL_ASYMMETRIC, BC_LEVEL_SEPARATION } bc_level_error_t;
typedef struct {
    uint8_t x, y, from_x, from_y, cooldown, alive, range, capacity, speed;
} bc_player_t;
typedef struct { uint8_t x, y, owner, fuse, range; } bc_bomb_t;
typedef struct {
    uint8_t tile[BC_CELLS], fire[BC_CELLS];
    bc_player_t players[BC_PLAYERS];
    bc_bomb_t bombs[BC_BOMBS];
    uint32_t random, tick;
    uint16_t timer, time_left;
    uint8_t phase, winner, round, score[BC_PLAYERS], theme;
} bc_world_t;
typedef struct {
    int32_t x, y, vx, vy;
    uint16_t life, color;
} bc_particle_t;
enum { BC_MUSIC_VOICES=5, BC_MUSIC_DELAY=3200, BC_MUSIC_TRACKS=3 };
typedef struct {
    uint32_t phase, step;
    uint16_t age, frames;
    uint8_t velocity;
} bc_music_voice_t;
typedef struct {
    uint32_t clock, noise;
    bc_music_voice_t music[BC_MUSIC_VOICES];
    uint16_t music_tick, music_frame, music_event, delay_at, drum_left[4];
    uint8_t music_track;
    int16_t delay[BC_MUSIC_DELAY];
    uint16_t effect_left[BC_SOUNDS];
    int32_t filter[BC_SOUNDS];
    uint8_t adpcm_index[BC_SOUNDS];
    int32_t music_gain;
    bool muted;
} bc_audio_t;
typedef struct {
    bc_world_t world;
    uint32_t accumulator, visual_ms, held;
    uint8_t humans, local_slot, events, direction[BC_PLAYERS];
    bool bomb_pressed[BC_PLAYERS], network, host, received;
    uint32_t generation, revision, assembling, received_mask;
    uint64_t seed;
    uint32_t last_sequence[BC_PLAYERS], input_age[BC_PLAYERS];
    uint16_t action[BC_PLAYERS], local_action;
    uint32_t input_ms, snapshot_ms, silence_ms;
    uint8_t assembly[BC_SNAPSHOT];
    uint8_t face[BC_PLAYERS];
    uint16_t death_ms[BC_PLAYERS], debris_ms[BC_CELLS], shake_ms, banner_ms;
    bc_particle_t particles[96];
    bc_audio_t audio;
    bool touch_seen, touch_down;
    uint8_t move_buffer;
    uint16_t move_buffer_ms;
    uint32_t seed_counter, cues;
    bc_level_t level, custom[BC_CUSTOM], undo;
    uint8_t selected, custom_slot, cursor_x, cursor_y, brush, editor_menu, level_error;
    bool mirror, undo_valid, testing, dirty;
    uint16_t edit_repeat, notice_ms;
    int16_t last_painted;
    uint8_t notice;
    bool stroke_changed;
    p4_game_save_ticket_t save_ticket;
    p4_game_save_status_t save_status;
    uint32_t save_sequence;
    uint8_t save_blob[BC_SAVE_BYTES];
    uint32_t level_id, level_hash, level_assembling, level_assembling_hash;
    uint32_t level_mask, transfer_ms;
    uint8_t level_ack, level_assembly[BC_LEVEL_BYTES];
    bool level_ready, snapshot_pending;
    uint8_t level_send_part, snapshot_part, outgoing_snapshot[BC_SNAPSHOT];

} bc_state_t;
extern const p4_game_descriptor_t p4_blast_circuit_game;
bool bc_wall(int x, int y);
bool bc_border(int x, int y);
bool bc_solid(uint8_t tile);
bool bc_destructible(uint8_t tile);
bool bc_pickup(uint8_t tile);
bool bc_blocked(const bc_world_t *world, int x, int y);
void bc_level_preset(bc_level_t *level, unsigned theme, uint32_t seed);
bc_level_error_t bc_level_validate(const bc_level_t *level);
bc_level_error_t bc_level_match_validate(const bc_level_t *level);
unsigned bc_level_route_blocks(const bc_level_t *level, unsigned from, unsigned to);
bool bc_spawn_safe(int x, int y);
void bc_level_apply(bc_world_t *world, const bc_level_t *level);
uint32_t bc_hash(const uint8_t *bytes, unsigned count);
void bc_level_encode(const bc_level_t *level, uint8_t out[BC_LEVEL_BYTES]);
bool bc_level_decode(bc_level_t *level, const uint8_t in[BC_LEVEL_BYTES]);
void bc_levels_load(p4_game_context_t *context, bc_state_t *state);
void bc_levels_save(p4_game_context_t *context, bc_state_t *state);
void bc_save_poll(p4_game_context_t *context, bc_state_t *state);
void bc_select_level(bc_state_t *state, unsigned selected);
void bc_editor_enter(bc_state_t *state);
void bc_editor_paint(bc_state_t *state, int x, int y);
void bc_editor_undo(bc_state_t *state);
bool bc_menu_update(p4_game_context_t *context, bc_state_t *state,
                    const p4_game_input_t *input, bool fresh_touch, uint32_t elapsed);
void bc_restart(bc_state_t *state);
void bc_start_battle(bc_state_t *state);
void bc_network_transfer(bc_state_t *state);
void bc_cue(bc_state_t *state, bc_sound_t sound);
void bc_round(bc_world_t *world, uint32_t seed, bool new_match);
bool bc_place(bc_world_t *world, unsigned slot);
uint8_t bc_step(bc_world_t *world, const uint8_t directions[BC_PLAYERS],
                const bool place[BC_PLAYERS]);
uint8_t bc_bot(bc_world_t *world, unsigned slot, bool *place);
void bc_encode(const bc_world_t *world, uint8_t bytes[BC_SNAPSHOT]);
bool bc_decode(bc_world_t *world, const uint8_t bytes[BC_SNAPSHOT]);
bool bc_network_begin(p4_game_context_t *context, bc_state_t *state);
bool bc_network_poll(p4_game_context_t *context, bc_state_t *state,
                     uint8_t direction, bool bomb, uint32_t elapsed);
void bc_network_publish(p4_game_context_t *context, bc_state_t *state);
void bc_effects(bc_state_t *state, const bc_world_t *previous);
void bc_visual_step(bc_state_t *state, uint32_t elapsed);
bool bc_render(p4_game_context_t *context, p4_game_surface_t *surface);
void bc_audio_update(p4_game_context_t *context, bc_state_t *state,
                     uint32_t elapsed);
#endif
