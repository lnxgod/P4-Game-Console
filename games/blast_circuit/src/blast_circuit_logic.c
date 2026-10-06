// SPDX-License-Identifier: MIT
// Original bounded grid simulation; no engine, ROM, or third-party assets.
#include "blast_circuit_internal.h"
#include <string.h>

static const int dx[4] = {0, 0, -1, 1};
static const int dy[4] = {-1, 1, 0, 0};
static int cell(int x, int y) { return y * BC_W + x; }
bool bc_wall(int x, int y)
{
    return x <= 0 || y <= 0 || x >= BC_W - 1 || y >= BC_H - 1 ||
           ((x % 2 == 0) && (y % 2 == 0));
}
static uint32_t random_next(bc_world_t *w)
{
    w->random = w->random * UINT32_C(1664525) + UINT32_C(1013904223);
    return w->random;
}
static int bomb_at(const bc_world_t *w, int x, int y)
{
    for (int i = 0; i < BC_BOMBS; ++i)
        if (w->bombs[i].fuse && w->bombs[i].x == x && w->bombs[i].y == y)
            return i;
    return -1;
}
static bool walkable(const bc_world_t *w, int x, int y)
{
    return !bc_blocked(w, x, y) && !bc_destructible(w->tile[cell(x, y)]) &&
           bomb_at(w, x, y) < 0;
}
void bc_round(bc_world_t *w, uint32_t seed, bool new_match)
{
    uint8_t scores[4];
    memcpy(scores, w->score, sizeof(scores));
    const uint8_t next = w->round == 255U ? 1U : (uint8_t)(w->round + 1U);
    memset(w, 0, sizeof(*w));
    if (!new_match) memcpy(w->score, scores, sizeof(scores));
    w->round = new_match ? 1U : next;
    w->random = seed ? seed : UINT32_C(0xb1a57c17);
    w->phase = BC_READY;
    w->timer = 60U;
    w->time_left = 2400U; // Two minutes, then a draw.
    w->winner = 255U;
    bc_level_t level;
    bc_level_preset(&level,0U,seed);
    bc_level_apply(w,&level);
    static const uint8_t positions[4][2] = {{1,1}, {BC_W-2,BC_H-2}, {BC_W-2,1}, {1,BC_H-2}};
    for (unsigned i = 0; i < BC_PLAYERS; ++i) {
        w->players[i] = (bc_player_t){
            .x = positions[i][0], .y = positions[i][1],
            .from_x = positions[i][0], .from_y = positions[i][1],
            .alive = 1U, .range = 2U, .capacity = 1U,
        };
    }
}
bool bc_place(bc_world_t *w, unsigned slot)
{
    if (slot >= BC_PLAYERS || w->phase != BC_PLAY) return false;
    bc_player_t *p = &w->players[slot];
    if (!p->alive || bomb_at(w, p->x, p->y) >= 0) return false;
    unsigned owned = 0U;
    int free_slot = -1;
    for (int i = 0; i < BC_BOMBS; ++i) {
        if (!w->bombs[i].fuse) free_slot = i;
        else if (w->bombs[i].owner == slot) ++owned;
    }
    if (owned >= p->capacity || free_slot < 0) return false;
    w->bombs[free_slot] = (bc_bomb_t){p->x, p->y, (uint8_t)slot,
                                    BC_FUSE, p->range};
    return true;
}
static bool burn(bc_world_t *w, int x, int y, bool pending[BC_BOMBS])
{
    if (bc_blocked(w, x, y)) return false;
    const int c = cell(x, y);
    if (w->fire[c] < BC_FIRE) w->fire[c] = BC_FIRE;
    if (w->tile[c] == BC_FUEL) {
        w->tile[c] = BC_FLOOR; w->fire[c] = BC_EMBER_FIRE; return false;
    }
    if (w->tile[c] == BC_ARMOR) { w->tile[c] = BC_DAMAGED; return false; }
    if (bc_destructible(w->tile[c])) {
        const uint32_t drop = random_next(w) % 8U;
        w->tile[c] = drop < 3U ? (uint8_t)(BC_RANGE + drop) : BC_FLOOR;
        return false;
    }
    if (bc_pickup(w->tile[c])) w->tile[c] = BC_FLOOR;
    const int b = bomb_at(w, x, y);
    if (b >= 0) { pending[b] = true; return false; }
    return true;
}
static uint8_t explosions(bc_world_t *w)
{
    bool pending[BC_BOMBS] = {false};
    bool done[BC_BOMBS] = {false};
    uint8_t event = 0U;
    for (int i = 0; i < BC_BOMBS; ++i) {
        bc_bomb_t *b = &w->bombs[i];
        if (b->fuse && (--b->fuse == 0U || w->fire[cell(b->x, b->y)])) {
            b->fuse = 1U;
            pending[i] = true;
        }
    }
    // Each bomb is processed once, including cascades back to lower indices.
    for (int pass = 0; pass < BC_BOMBS; ++pass) {
        int selected = -1;
        for (int i = 0; i < BC_BOMBS; ++i)
            if (pending[i] && !done[i]) { selected = i; break; }
        if (selected < 0) break;
        done[selected] = true;
        const bc_bomb_t b = w->bombs[selected];
        w->bombs[selected].fuse = 0U;
        event |= BC_EVENT_BLAST;
        (void)burn(w, b.x, b.y, pending);
        for (int d = 0; d < 4; ++d)
            for (int r = 1; r <= b.range; ++r)
                if (!burn(w, b.x + dx[d] * r, b.y + dy[d] * r, pending)) break;
    }
    return event;
}
uint8_t bc_step(bc_world_t *w, const uint8_t dirs[4], const bool place[4])
{
    uint8_t events = 0U;
    if (w->phase == BC_READY) {
        if (w->timer && --w->timer == 0U) w->phase = BC_PLAY;
        return 0U;
    }
    if (w->phase == BC_ROUND) {
        if (w->timer && --w->timer == 0U) bc_round(w, w->random, false);
        return 0U;
    }
    if (w->phase != BC_PLAY) return 0U;
    ++w->tick;
    if (w->time_left) --w->time_left;
    for (int c = 0; c < BC_CELLS; ++c) if (w->fire[c]) --w->fire[c];
    // Telegraph at thirty seconds. At twenty-five, the reactor begins closing
    // one ring every five seconds, preventing indefinitely safe corner camping.
    if (w->time_left <= 500U) {
        const int ring = 1 + (500 - (int)w->time_left) / 100;
        for (int y = 1; y < BC_H-1; ++y) for (int x = 1; x < BC_W-1; ++x) {
            if (bc_blocked(w,x,y)) continue;
            if (x <= ring || y <= ring || x >= BC_W-1-ring || y >= BC_H-1-ring) {
                w->tile[cell(x,y)] = BC_FLOOR;
                w->fire[cell(x,y)] = BC_FIRE;
            }
        }
    }
    for (unsigned i = 0; i < BC_PLAYERS; ++i) {
        bc_player_t *p = &w->players[i];
        // A player already in fire cannot escape between simulation ticks.
        if (w->fire[cell(p->x, p->y)]) p->alive = 0U;
        if (!p->alive) continue;
        if (place[i] && bc_place(w, i)) events |= BC_EVENT_BOMB;
        if (p->cooldown) --p->cooldown;
        if (!p->cooldown) {
            p->from_x = p->x; p->from_y = p->y;
            for (unsigned d = 0; d < 4U; ++d) {
                if (!(dirs[i] & (1U << d))) continue;
                const int nx = p->x + dx[d], ny = p->y + dy[d];
                if (walkable(w, nx, ny)) {
                    p->x = (uint8_t)nx; p->y = (uint8_t)ny;
                    p->cooldown = p->speed ? 2U : 3U;
                }
                break;
            }
        }
        const int c = cell(p->x, p->y);
        if (w->fire[c]) { p->alive = 0U; continue; }
        if (bc_pickup(w->tile[c])) {
            if (w->tile[c] == BC_RANGE && p->range < 5U) ++p->range;
            if (w->tile[c] == BC_EXTRA && p->capacity < 3U) ++p->capacity;
            if (w->tile[c] == BC_SPEED) {
                p->speed = 1U;
                if (p->cooldown > 2U) p->cooldown = 2U;
            }
            w->tile[c] = BC_FLOOR;
            events |= BC_EVENT_PICKUP;
        }
    }
    events |= explosions(w);
    unsigned alive = 0U;
    uint8_t winner = 255U;
    for (unsigned i = 0; i < BC_PLAYERS; ++i) {
        bc_player_t *p = &w->players[i];
        if (w->fire[cell(p->x, p->y)]) p->alive = 0U;
        if (p->alive) { ++alive; winner = (uint8_t)i; }
    }
    if (alive <= 1U || !w->time_left) {
        w->winner = alive == 1U ? winner : 255U;
        if (w->winner != 255U) ++w->score[winner];
        w->phase = w->winner != 255U && w->score[winner] >= BC_WIN_SCORE
            ? BC_MATCH : BC_ROUND;
        w->timer = 60U;
        events |= BC_EVENT_WIN;
    }
    return events;
}
static void danger_map(const bc_world_t *w, bool danger[BC_CELLS])
{
    for (int c = 0; c < BC_CELLS; ++c) danger[c] = w->fire[c] != 0U;
    for (int i = 0; i < BC_BOMBS; ++i) {
        const bc_bomb_t *b = &w->bombs[i];
        if (!b->fuse) continue;
        danger[cell(b->x, b->y)] = true;
        for (int d = 0; d < 4; ++d) {
            for (int r = 1; r <= b->range; ++r) {
                const int x = b->x + dx[d] * r, y = b->y + dy[d] * r;
                if (bc_blocked(w, x, y)) break;
                danger[cell(x, y)] = true;
                if (bc_destructible(w->tile[cell(x, y)])) break;
            }
        }
    }
}
static bool target_cell(const bc_world_t *w, int x, int y, unsigned slot)
{
    if (bc_pickup(w->tile[cell(x, y)])) return true;
    for (int d = 0; d < 4; ++d)
        if (!bc_blocked(w, x + dx[d], y + dy[d]) &&
            bc_destructible(w->tile[cell(x + dx[d], y + dy[d])])) return true;
    for (unsigned i = 0; i < BC_PLAYERS; ++i) {
        if (i == slot || !w->players[i].alive) continue;
        const int ax = (int)w->players[i].x - x, ay = (int)w->players[i].y - y;
        if ((ax == 0 && ay >= -2 && ay <= 2) ||
            (ay == 0 && ax >= -2 && ax <= 2)) return true;
    }
    return false;
}
// Bounded breadth-first path; return the first direction, with a maximum path
// short enough to escape a freshly placed bomb before its fuse expires.
static uint8_t route(const bc_world_t *w, unsigned slot,
                     const bool danger[BC_CELLS], bool escaping, bool *found)
{
    uint16_t queue[BC_CELLS];
    uint8_t first[BC_CELLS] = {0}, depth[BC_CELLS] = {0};
    bool seen[BC_CELLS] = {false};
    const bc_player_t *p = &w->players[slot];
    const int start = cell(p->x, p->y);
    unsigned head = 0U, tail = 1U;
    queue[0] = (uint16_t)start; seen[start] = true; *found = false;
    while (head < tail) {
        const int c = queue[head++], x = c % BC_W, y = c / BC_W;
        if (!danger[c] && (escaping || target_cell(w, x, y, slot))) {
            *found = true; return first[c];
        }
        if (depth[c] >= 10U) continue;
        for (unsigned k = 0; k < 4U; ++k) {
            const unsigned d = (k + slot + (w->tick / 40U)) % 4U;
            const int nx = x + dx[d], ny = y + dy[d];
            if (!walkable(w, nx, ny)) continue;
            const int n = cell(nx, ny);
            if (seen[n] || w->fire[n] || (!escaping && danger[n])) continue;
            seen[n] = true; depth[n] = (uint8_t)(depth[c] + 1U);
            first[n] = c == start ? (uint8_t)(1U << d) : first[c];
            queue[tail++] = (uint16_t)n;
        }
    }
    return 0U;
}
uint8_t bc_bot(bc_world_t *w, unsigned slot, bool *place)
{
    *place = false;
    if (slot >= BC_PLAYERS || !w->players[slot].alive || w->phase != BC_PLAY)
        return 0U;
    bool danger[BC_CELLS], found;
    danger_map(w, danger);
    bc_player_t *p = &w->players[slot];
    if (danger[cell(p->x, p->y)]) return route(w, slot, danger, true, &found);
    if (p->cooldown) return 0U;
    if (target_cell(w, p->x, p->y, slot)) {
        // Test a real placement in a temporary world; never suicide knowingly.
        bc_world_t future = *w;
        if (bc_place(&future, slot)) {
            danger_map(&future, danger);
            const uint8_t escape = route(&future, slot, danger, true, &found);
            if (found && escape) { *place = true; return escape; }
        }
        danger_map(w, danger);
    }
    return route(w, slot, danger, false, &found);
}
