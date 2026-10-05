// SPDX-License-Identifier: MIT
#include "p4/dice_accessory.h"
#include <string.h>
bool p4_dice_request_valid(const p4_dice_request_t *r)
{
    if (!r || !r->token || r->player_slot >= 4 || !r->count ||
        r->count > P4_DICE_MAX || r->sides < 2 ||
        ((unsigned)r->held_mask >> r->count)) return false;
    size_t n = 0;
    for (; n < P4_DICE_NAME_BYTES && r->player_name[n]; ++n)
        if ((unsigned char)r->player_name[n] < 32 ||
            (unsigned char)r->player_name[n] > 126) return false;
    if (!n || n == P4_DICE_NAME_BYTES) return false;
    for (size_t i = 0; i < P4_DICE_MAX; ++i)
        if (i < r->count ? (r->faces[i] < 1 || r->faces[i] > r->sides)
                         : r->faces[i] != 0) return false;
    return true;
}
bool p4_dice_encode(const p4_dice_request_t *r, p4_dice_phase_t phase,
    uint8_t kind, uint8_t b[P4_DICE_WIRE_BYTES])
{
    if (!b || !p4_dice_request_valid(r) || phase > P4_DICE_ROLLED ||
        phase < P4_DICE_OFFLINE || (kind != 1 && kind != 2)) return false;
    memset(b, 0, P4_DICE_WIRE_BYTES);
    b[0] = 2; b[1] = kind; b[2] = (uint8_t)phase;
    b[3] = (r->enabled ? 1U : 0U) | (r->can_hold ? 2U : 0U);
    for (unsigned i = 0; i < 4; ++i) b[4+i] = (uint8_t)(r->token >> (8*i));
    b[8] = r->player_slot; b[9] = r->count; b[10] = r->sides; b[11] = r->held_mask;
    memcpy(b+12, r->faces, P4_DICE_MAX);
    for (size_t i = 0; i < P4_DICE_NAME_BYTES && r->player_name[i]; ++i)
        b[20+i] = (uint8_t)r->player_name[i];
    b[40]=(uint8_t)r->hold_ack;b[41]=(uint8_t)(r->hold_ack>>8U);
    return true;
}
bool p4_dice_decode(const uint8_t *b, size_t n, uint8_t kind,
    p4_dice_request_t *r, p4_dice_phase_t *phase)
{
    if (!b || !r || !phase || n != P4_DICE_WIRE_BYTES || b[0] != 2 ||
        b[1] != kind || (kind != 1 && kind != 2) || b[2] > P4_DICE_ROLLED ||
        b[3] > 3) return false;
    for (size_t i = 42; i < n; ++i) if (b[i]) return false;
    p4_dice_request_t v = {0};
    for (unsigned i = 0; i < 4; ++i) v.token |= (uint32_t)b[4+i] << (8*i);
    v.enabled = (b[3] & 1U) != 0; v.can_hold = (b[3] & 2U) != 0; v.player_slot = b[8]; v.count = b[9];
    v.sides = b[10]; v.held_mask = b[11];
    memcpy(v.faces,b+12,P4_DICE_MAX); memcpy(v.player_name,b+20,P4_DICE_NAME_BYTES);
    v.hold_ack=(uint16_t)((uint16_t)b[40] | ((uint16_t)b[41]<<8U));
    if (!p4_dice_request_valid(&v)) return false;
    bool ended = false;
    for (size_t i=0; i<P4_DICE_NAME_BYTES; ++i) {
        if (!v.player_name[i]) ended=true;
        else if (ended) return false;
    }
    *r = v; *phase = (p4_dice_phase_t)b[2]; return true;
}
void p4_dice_gesture_reset(p4_dice_gesture_t *g, uint32_t token)
{
    if (g) *g = (p4_dice_gesture_t){.token=token,.phase=P4_DICE_WAITING};
}
bool p4_dice_gesture_ready(p4_dice_gesture_t *g, uint32_t now)
{
    if (!g || !g->token || g->phase != P4_DICE_WAITING) return false;
    g->phase=P4_DICE_READY; g->armed_ms=now; return true;
}
bool p4_dice_gesture_sample(p4_dice_gesture_t *g, uint32_t now,
    int32_t x, int32_t y, int32_t z)
{
    if (!g || (g->phase != P4_DICE_READY && g->phase != P4_DICE_SHAKING)) return false;
    if (now-g->armed_ms > 15000) { p4_dice_gesture_reset(g,g->token); return false; }
    /* Reject invalid/saturated samples; bound before squaring. */
    if (x < -16000 || x > 16000 || y < -16000 || y > 16000 || z < -16000 || z > 16000)
        return false;
    int64_t magnitude=(int64_t)x*x+(int64_t)y*y+(int64_t)z*z;
    bool high=magnitude > 3240000; /* 1.8 g */
    if (high) {
        g->quiet_ms=now;
        if (!g->above && (g->peaks == 0 || now-g->last_peak_ms >= 80)) {
            if (!g->peaks) g->first_peak_ms=now;
            if (g->peaks < 255) ++g->peaks;
            g->last_peak_ms=now;
            g->phase=P4_DICE_SHAKING;
        }
        g->above=true;
    } else if (magnitude < 1690000) { g->above=false; }
    else { g->quiet_ms=now; }
    if (g->peaks >= 3 && now-g->first_peak_ms >= 300 &&
        now-g->quiet_ms >= 350) {
        g->phase=P4_DICE_ROLLED; return true;
    }
    if (g->peaks && now-g->last_peak_ms > 2000) {
        g->peaks=0; g->above=false; g->phase=P4_DICE_READY;
    }
    return false;
}

/* The accessory may change only held dice in a WAITING response, and only
 * when the current game request permits it. Every other field is echoed. */
bool p4_dice_accept_status(const p4_dice_request_t *wanted,
    const p4_dice_request_t *reply, p4_dice_phase_t phase,
    p4_dice_status_t *out)
{
    uint8_t a[P4_DICE_WIRE_BYTES], b[P4_DICE_WIRE_BYTES];
    if (!out || !p4_dice_encode(wanted,P4_DICE_WAITING,P4_DICE_REQUEST,a) ||
        !p4_dice_encode(reply,P4_DICE_WAITING,P4_DICE_REQUEST,b) ||
        phase < P4_DICE_WAITING || phase > P4_DICE_ROLLED) return false;
    const bool changed = wanted->hold_ack != reply->hold_ack;
    if (!changed && wanted->held_mask != reply->held_mask) return false;
    if (changed && (!wanted->can_hold || phase != P4_DICE_WAITING)) return false;
    b[11] = a[11];b[40]=a[40];b[41]=a[41];
    if (memcmp(a,b,sizeof(a))) return false;
    *out=(p4_dice_status_t){.token=reply->token,.phase=phase,
        .player_slot=reply->player_slot,.held_mask=reply->held_mask,
        .hold_changed=changed,.hold_sequence=reply->hold_ack};
    return true;
}
