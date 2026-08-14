// SPDX-License-Identifier: GPL-2.0-or-later

#include "doom/music_synth.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "doom/audio_mixer.h"

#define DOOM_MUS_HEADER_BYTES ((size_t)16U)
#define DOOM_MUS_MAGIC_0 UINT8_C(0x4d)
#define DOOM_MUS_MAGIC_1 UINT8_C(0x55)
#define DOOM_MUS_MAGIC_2 UINT8_C(0x53)
#define DOOM_MUS_MAGIC_3 UINT8_C(0x1a)
#define DOOM_MUS_EVENT_RELEASE UINT8_C(0)
#define DOOM_MUS_EVENT_PLAY UINT8_C(1)
#define DOOM_MUS_EVENT_PITCH UINT8_C(2)
#define DOOM_MUS_EVENT_SYSTEM UINT8_C(3)
#define DOOM_MUS_EVENT_CONTROLLER UINT8_C(4)
#define DOOM_MUS_EVENT_END UINT8_C(6)
#define DOOM_MUS_MAX_EVENTS_WITHOUT_DELAY UINT32_C(4096)
#define DOOM_MUS_MAX_DELAY_TICKS UINT32_C(1000000)
#define DOOM_MUSIC_MASTER_MAX UINT32_C(127)
#define DOOM_MUSIC_ENVELOPE_MAX UINT16_C(32767)
#define DOOM_MUSIC_ATTACK_STEP UINT16_C(512)
#define DOOM_MUSIC_RELEASE_STEP UINT16_C(32)
#define DOOM_MUSIC_PERCUSSION_DECAY UINT16_C(16)
#define DOOM_MUSIC_VOICE_DIVISOR INT32_C(12)

struct doom_music_song {
    atomic_uint_least32_t references;
    size_t length;
    uint8_t data[];
};

typedef struct {
    const uint8_t *bytes;
    size_t length;
    size_t cursor;
} mus_cursor_t;

static uint16_t read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static bool cursor_read_u8(mus_cursor_t *cursor, uint8_t *out_value)
{
    if (cursor == NULL || out_value == NULL ||
        cursor->cursor >= cursor->length) {
        return false;
    }
    *out_value = cursor->bytes[cursor->cursor++];
    return true;
}

static bool cursor_read_delay(mus_cursor_t *cursor, uint32_t *out_ticks)
{
    if (cursor == NULL || out_ticks == NULL) {
        return false;
    }
    uint32_t value = 0U;
    for (unsigned byte_index = 0U; byte_index < 5U; ++byte_index) {
        uint8_t byte = 0U;
        if (!cursor_read_u8(cursor, &byte) ||
            value > (DOOM_MUS_MAX_DELAY_TICKS >> 7U)) {
            return false;
        }
        value = (value << 7U) | (uint32_t)(byte & UINT8_C(0x7f));
        if (value > DOOM_MUS_MAX_DELAY_TICKS) {
            return false;
        }
        if ((byte & UINT8_C(0x80)) == 0U) {
            *out_ticks = value;
            return true;
        }
    }
    return false;
}

static bool validate_score(const uint8_t *score, size_t score_bytes)
{
    mus_cursor_t cursor = {
        .bytes = score,
        .length = score_bytes,
        .cursor = 0U,
    };
    uint32_t events_without_delay = 0U;
    while (cursor.cursor < cursor.length) {
        uint8_t descriptor = 0U;
        if (!cursor_read_u8(&cursor, &descriptor)) {
            return false;
        }
        const uint8_t event = (uint8_t)((descriptor >> 4U) & UINT8_C(0x07));
        uint8_t value = 0U;
        switch (event) {
        case DOOM_MUS_EVENT_RELEASE:
        case DOOM_MUS_EVENT_PITCH:
            if (!cursor_read_u8(&cursor, &value)) {
                return false;
            }
            break;
        case DOOM_MUS_EVENT_PLAY:
            if (!cursor_read_u8(&cursor, &value)) {
                return false;
            }
            if ((value & UINT8_C(0x80)) != 0U &&
                !cursor_read_u8(&cursor, &value)) {
                return false;
            }
            break;
        case DOOM_MUS_EVENT_SYSTEM:
            if (!cursor_read_u8(&cursor, &value) || value < UINT8_C(10) ||
                value > UINT8_C(14)) {
                return false;
            }
            break;
        case DOOM_MUS_EVENT_CONTROLLER: {
            uint8_t controller = 0U;
            if (!cursor_read_u8(&cursor, &controller) ||
                !cursor_read_u8(&cursor, &value) ||
                controller > UINT8_C(9)) {
                return false;
            }
            break;
        }
        case DOOM_MUS_EVENT_END:
            return true;
        default:
            return false;
        }

        ++events_without_delay;
        if (events_without_delay > DOOM_MUS_MAX_EVENTS_WITHOUT_DELAY) {
            return false;
        }
        if ((descriptor & UINT8_C(0x80)) != 0U) {
            uint32_t delay = 0U;
            if (!cursor_read_delay(&cursor, &delay)) {
                return false;
            }
            events_without_delay = 0U;
        }
    }
    return false;
}

bool doom_music_validate_mus(const uint8_t *data, size_t length)
{
    if (data == NULL || length < DOOM_MUS_HEADER_BYTES ||
        length > (size_t)DOOM_MUSIC_MAX_SONG_BYTES ||
        data[0] != DOOM_MUS_MAGIC_0 || data[1] != DOOM_MUS_MAGIC_1 ||
        data[2] != DOOM_MUS_MAGIC_2 || data[3] != DOOM_MUS_MAGIC_3) {
        return false;
    }
    const size_t score_bytes = (size_t)read_u16_le(&data[4]);
    const size_t score_start = (size_t)read_u16_le(&data[6]);
    const size_t instrument_count = (size_t)read_u16_le(&data[12]);
    if (instrument_count > (SIZE_MAX - DOOM_MUS_HEADER_BYTES) / 2U) {
        return false;
    }
    const size_t minimum_score_start =
        DOOM_MUS_HEADER_BYTES + instrument_count * 2U;
    if (score_bytes == 0U || score_start < minimum_score_start ||
        score_start > length || score_bytes > length - score_start) {
        return false;
    }
    return validate_score(&data[score_start], score_bytes);
}

doom_music_song_t *doom_music_song_create(const void *data, size_t length)
{
    if (data == NULL || length > SIZE_MAX - sizeof(doom_music_song_t) ||
        !doom_music_validate_mus(data, length)) {
        return NULL;
    }
    doom_music_song_t *const song = malloc(sizeof(*song) + length);
    if (song == NULL) {
        return NULL;
    }
    atomic_init(&song->references, UINT32_C(1));
    song->length = length;
    memcpy(song->data, data, length);
    return song;
}

bool doom_music_song_retain(doom_music_song_t *song)
{
    if (song == NULL) {
        return false;
    }
    uint32_t references = atomic_load_explicit(
        &song->references, memory_order_relaxed);
    while (references != 0U && references != UINT32_MAX) {
        if (atomic_compare_exchange_weak_explicit(
                &song->references, &references, references + UINT32_C(1),
                memory_order_acquire, memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

void doom_music_song_release(doom_music_song_t *song)
{
    if (song == NULL) {
        return;
    }
    const uint32_t previous = atomic_fetch_sub_explicit(
        &song->references, UINT32_C(1), memory_order_acq_rel);
    if (previous == UINT32_C(1)) {
        free(song);
    }
}

size_t doom_music_song_length(const doom_music_song_t *song)
{
    return song != NULL ? song->length : 0U;
}

static void initialize_channels(doom_music_player_t *player)
{
    for (size_t index = 0U; index < (size_t)DOOM_MUSIC_CHANNEL_COUNT;
         ++index) {
        player->channels[index] = (doom_music_channel_t){
            .program = 0U,
            .volume = UINT8_C(127),
            .expression = UINT8_C(127),
            .pan = UINT8_C(64),
            .bend = UINT8_C(128),
            .last_velocity = UINT8_C(127),
        };
    }
}

void doom_music_player_init(doom_music_player_t *player)
{
    if (player == NULL) {
        return;
    }
    memset(player, 0, sizeof(*player));
    player->volume = UINT8_C(64);
    player->noise_state = UINT32_C(0x6d2b79f5);
    initialize_channels(player);
}

static void release_player_song(doom_music_player_t *player)
{
    doom_music_song_t *const song = player->song;
    player->song = NULL;
    player->score = NULL;
    player->score_bytes = 0U;
    player->cursor = 0U;
    player->samples_until_event = 0U;
    player->timing_remainder = 0U;
    player->playing = false;
    player->paused = false;
    memset(player->voices, 0, sizeof(player->voices));
    if (song != NULL) {
        doom_music_song_release(song);
    }
}

void doom_music_player_stop(doom_music_player_t *player)
{
    if (player != NULL) {
        release_player_song(player);
        player->stats.playing = false;
        player->stats.paused = false;
    }
}

bool doom_music_player_start(doom_music_player_t *player,
                             doom_music_song_t *song,
                             bool looping)
{
    if (player == NULL || song == NULL ||
        !doom_music_song_retain(song)) {
        return false;
    }
    const size_t score_start = (size_t)read_u16_le(&song->data[6]);
    const size_t score_bytes = (size_t)read_u16_le(&song->data[4]);
    if (score_start > song->length || score_bytes > song->length - score_start) {
        doom_music_song_release(song);
        return false;
    }
    release_player_song(player);
    player->song = song;
    player->score = &song->data[score_start];
    player->score_bytes = score_bytes;
    player->cursor = 0U;
    player->samples_until_event = 0U;
    player->timing_remainder = 0U;
    player->looping = looping;
    player->playing = true;
    player->paused = false;
    player->gain_dirty = true;
    memset(player->voices, 0, sizeof(player->voices));
    initialize_channels(player);
    ++player->stats.songs_started;
    player->stats.playing = true;
    player->stats.paused = false;
    return true;
}

void doom_music_player_pause(doom_music_player_t *player)
{
    if (player != NULL && player->playing) {
        player->paused = true;
        player->stats.paused = true;
    }
}

void doom_music_player_resume(doom_music_player_t *player)
{
    if (player != NULL && player->playing) {
        player->paused = false;
        player->stats.paused = false;
    }
}

bool doom_music_player_set_volume(doom_music_player_t *player,
                                  uint8_t volume)
{
    if (player == NULL || volume > UINT8_C(127)) {
        return false;
    }
    player->volume = volume;
    player->gain_dirty = true;
    return true;
}

bool doom_music_player_is_playing(const doom_music_player_t *player)
{
    return player != NULL && player->playing;
}

static const uint32_t s_note_zero_phase_steps[12] = {
    UINT32_C(2194674), UINT32_C(2325176), UINT32_C(2463439),
    UINT32_C(2609922), UINT32_C(2765116), UINT32_C(2929539),
    UINT32_C(3103738), UINT32_C(3288296), UINT32_C(3483828),
    UINT32_C(3690988), UINT32_C(3910465), UINT32_C(4142993),
};

static uint32_t note_phase_step(uint8_t note)
{
    const uint8_t bounded = note > UINT8_C(127) ? UINT8_C(127) : note;
    const unsigned octave = (unsigned)(bounded / UINT8_C(12));
    const unsigned semitone = (unsigned)(bounded % UINT8_C(12));
    return s_note_zero_phase_steps[semitone] << octave;
}

static uint32_t bent_phase_step(uint8_t note, uint8_t bend)
{
    const uint32_t base = note_phase_step(note);
    if (bend >= UINT8_C(128)) {
        const uint8_t target_note = note > UINT8_C(125)
            ? UINT8_C(127) : (uint8_t)(note + UINT8_C(2));
        const uint32_t target = note_phase_step(target_note);
        return base + (uint32_t)(((uint64_t)(target - base) *
                                  (uint64_t)(bend - UINT8_C(128))) /
                                 UINT64_C(127));
    }
    const uint8_t target_note = note < UINT8_C(2)
        ? UINT8_C(0) : (uint8_t)(note - UINT8_C(2));
    const uint32_t target = note_phase_step(target_note);
    return base - (uint32_t)(((uint64_t)(base - target) *
                              (uint64_t)(UINT8_C(128) - bend)) /
                             UINT64_C(128));
}

static doom_music_waveform_t waveform_for_program(uint8_t program)
{
    if (program < UINT8_C(8)) {
        return DOOM_MUSIC_WAVE_SINE;
    }
    if (program < UINT8_C(24)) {
        return DOOM_MUSIC_WAVE_SQUARE;
    }
    if (program < UINT8_C(40)) {
        return DOOM_MUSIC_WAVE_SAW;
    }
    if (program < UINT8_C(56)) {
        return DOOM_MUSIC_WAVE_TRIANGLE;
    }
    if (program < UINT8_C(80)) {
        return DOOM_MUSIC_WAVE_SAW;
    }
    if (program < UINT8_C(104)) {
        return DOOM_MUSIC_WAVE_SINE;
    }
    return DOOM_MUSIC_WAVE_SQUARE;
}

static doom_music_voice_t *allocate_voice(doom_music_player_t *player,
                                          uint8_t channel,
                                          uint8_t note)
{
    doom_music_voice_t *candidate = NULL;
    for (size_t index = 0U; index < (size_t)DOOM_MUSIC_MAX_VOICES; ++index) {
        doom_music_voice_t *const voice = &player->voices[index];
        if (voice->active && voice->channel == channel && voice->note == note) {
            return voice;
        }
        if (!voice->active) {
            return voice;
        }
        if (candidate == NULL ||
            (voice->releasing && !candidate->releasing) ||
            (voice->releasing == candidate->releasing &&
             voice->age < candidate->age)) {
            candidate = voice;
        }
    }
    return candidate;
}

static void note_on(doom_music_player_t *player,
                    uint8_t channel,
                    uint8_t note,
                    uint8_t velocity)
{
    doom_music_voice_t *const voice = allocate_voice(player, channel, note);
    if (voice == NULL) {
        return;
    }
    const bool percussion = channel == UINT8_C(15);
    const doom_music_channel_t *const channel_state =
        &player->channels[channel];
    *voice = (doom_music_voice_t){
        .phase = 0U,
        .phase_step = bent_phase_step(note, channel_state->bend),
        .age = ++player->voice_age,
        .envelope_q15 = percussion ? DOOM_MUSIC_ENVELOPE_MAX : 0U,
        .channel = channel,
        .note = note,
        .velocity = velocity,
        .waveform = percussion ? DOOM_MUSIC_WAVE_NOISE
                               : waveform_for_program(channel_state->program),
        .active = true,
        .releasing = false,
        .percussion = percussion,
    };
    player->gain_dirty = true;
    ++player->stats.notes_started;
}

static void note_off(doom_music_player_t *player,
                     uint8_t channel,
                     uint8_t note)
{
    for (size_t index = 0U; index < (size_t)DOOM_MUSIC_MAX_VOICES; ++index) {
        doom_music_voice_t *const voice = &player->voices[index];
        if (voice->active && voice->channel == channel && voice->note == note) {
            voice->releasing = true;
        }
    }
}

static void all_notes_off(doom_music_player_t *player, uint8_t channel,
                          bool immediate)
{
    for (size_t index = 0U; index < (size_t)DOOM_MUSIC_MAX_VOICES; ++index) {
        doom_music_voice_t *const voice = &player->voices[index];
        if (voice->active && voice->channel == channel) {
            if (immediate) {
                memset(voice, 0, sizeof(*voice));
            } else {
                voice->releasing = true;
            }
        }
    }
}

static void update_channel_pitch(doom_music_player_t *player, uint8_t channel)
{
    const uint8_t bend = player->channels[channel].bend;
    for (size_t index = 0U; index < (size_t)DOOM_MUSIC_MAX_VOICES; ++index) {
        doom_music_voice_t *const voice = &player->voices[index];
        if (voice->active && voice->channel == channel && !voice->percussion) {
            voice->phase_step = bent_phase_step(voice->note, bend);
        }
    }
}

static void reset_channel(doom_music_player_t *player, uint8_t channel)
{
    player->channels[channel] = (doom_music_channel_t){
        .program = 0U,
        .volume = UINT8_C(127),
        .expression = UINT8_C(127),
        .pan = UINT8_C(64),
        .bend = UINT8_C(128),
        .last_velocity = UINT8_C(127),
    };
    all_notes_off(player, channel, false);
    player->gain_dirty = true;
}

static void apply_system_event(doom_music_player_t *player,
                               uint8_t channel,
                               uint8_t system_event)
{
    if (system_event == UINT8_C(10)) {
        all_notes_off(player, channel, true);
    } else if (system_event == UINT8_C(11)) {
        all_notes_off(player, channel, false);
    } else if (system_event == UINT8_C(14)) {
        reset_channel(player, channel);
    }
}

static void apply_controller(doom_music_player_t *player,
                             uint8_t channel,
                             uint8_t controller,
                             uint8_t value)
{
    doom_music_channel_t *const state = &player->channels[channel];
    const uint8_t bounded = value > UINT8_C(127) ? UINT8_C(127) : value;
    switch (controller) {
    case 0:
        state->program = bounded;
        break;
    case 3:
        state->volume = bounded;
        break;
    case 4:
        state->pan = bounded;
        break;
    case 5:
        state->expression = bounded;
        break;
    default:
        break;
    }
    player->gain_dirty = true;
}

static void finish_or_loop(doom_music_player_t *player)
{
    if (!player->looping) {
        release_player_song(player);
        player->stats.playing = false;
        player->stats.paused = false;
        return;
    }
    memset(player->voices, 0, sizeof(player->voices));
    initialize_channels(player);
    player->cursor = 0U;
    player->samples_until_event = 0U;
    player->timing_remainder = 0U;
    ++player->stats.loops_completed;
}

static bool runtime_read_u8(doom_music_player_t *player, uint8_t *out_value)
{
    if (player->cursor >= player->score_bytes) {
        return false;
    }
    *out_value = player->score[player->cursor++];
    return true;
}

static bool runtime_read_delay(doom_music_player_t *player,
                               uint32_t *out_ticks)
{
    mus_cursor_t cursor = {
        .bytes = player->score,
        .length = player->score_bytes,
        .cursor = player->cursor,
    };
    if (!cursor_read_delay(&cursor, out_ticks)) {
        return false;
    }
    player->cursor = cursor.cursor;
    return true;
}

static void fail_quiet(doom_music_player_t *player)
{
    ++player->stats.parse_failures;
    release_player_song(player);
    player->stats.playing = false;
    player->stats.paused = false;
}

static bool process_events(doom_music_player_t *player)
{
    uint32_t events = 0U;
    while (player->playing && player->samples_until_event == 0U) {
        if (++events > DOOM_MUS_MAX_EVENTS_WITHOUT_DELAY) {
            fail_quiet(player);
            return false;
        }
        uint8_t descriptor = 0U;
        if (!runtime_read_u8(player, &descriptor)) {
            fail_quiet(player);
            return false;
        }
        const uint8_t channel = descriptor & UINT8_C(0x0f);
        const uint8_t event =
            (uint8_t)((descriptor >> 4U) & UINT8_C(0x07));
        uint8_t value = 0U;
        switch (event) {
        case DOOM_MUS_EVENT_RELEASE:
            if (!runtime_read_u8(player, &value)) {
                fail_quiet(player);
                return false;
            }
            note_off(player, channel, value & UINT8_C(0x7f));
            break;
        case DOOM_MUS_EVENT_PLAY: {
            if (!runtime_read_u8(player, &value)) {
                fail_quiet(player);
                return false;
            }
            const uint8_t note = value & UINT8_C(0x7f);
            if ((value & UINT8_C(0x80)) != 0U) {
                if (!runtime_read_u8(player, &value)) {
                    fail_quiet(player);
                    return false;
                }
                player->channels[channel].last_velocity =
                    value & UINT8_C(0x7f);
            }
            note_on(player, channel, note,
                    player->channels[channel].last_velocity);
            break;
        }
        case DOOM_MUS_EVENT_PITCH:
            if (!runtime_read_u8(player, &value)) {
                fail_quiet(player);
                return false;
            }
            player->channels[channel].bend = value;
            update_channel_pitch(player, channel);
            break;
        case DOOM_MUS_EVENT_SYSTEM:
            if (!runtime_read_u8(player, &value) || value < UINT8_C(10) ||
                value > UINT8_C(14)) {
                fail_quiet(player);
                return false;
            }
            apply_system_event(player, channel, value);
            break;
        case DOOM_MUS_EVENT_CONTROLLER: {
            uint8_t controller = 0U;
            if (!runtime_read_u8(player, &controller) ||
                !runtime_read_u8(player, &value) ||
                controller > UINT8_C(9)) {
                fail_quiet(player);
                return false;
            }
            apply_controller(player, channel, controller, value);
            break;
        }
        case DOOM_MUS_EVENT_END:
            ++player->stats.events_processed;
            finish_or_loop(player);
            continue;
        default:
            fail_quiet(player);
            return false;
        }
        ++player->stats.events_processed;

        if ((descriptor & UINT8_C(0x80)) != 0U) {
            uint32_t ticks = 0U;
            if (!runtime_read_delay(player, &ticks)) {
                fail_quiet(player);
                return false;
            }
            const uint64_t numerator =
                (uint64_t)ticks * (uint64_t)DOOM_AUDIO_OUTPUT_RATE_HZ +
                (uint64_t)player->timing_remainder;
            player->samples_until_event =
                (uint32_t)(numerator /
                           (uint64_t)DOOM_MUSIC_TICKS_PER_SECOND);
            player->timing_remainder =
                (uint32_t)(numerator %
                           (uint64_t)DOOM_MUSIC_TICKS_PER_SECOND);
            if (ticks != 0U && player->samples_until_event == 0U) {
                player->samples_until_event = 1U;
            }
        }
    }
    return true;
}

static int32_t waveform_sample(doom_music_player_t *player,
                               doom_music_voice_t *voice)
{
    int32_t sample = 0;
    const uint16_t phase = (uint16_t)(voice->phase >> 16U);
    switch (voice->waveform) {
    case DOOM_MUSIC_WAVE_SINE: {
        const int32_t x = (int32_t)(int16_t)phase;
        const int32_t magnitude = x < 0 ? -x : x;
        sample = (int32_t)((INT64_C(4) * (int64_t)x *
                            (int64_t)(INT32_C(32768) - magnitude)) /
                           INT64_C(32768));
        break;
    }
    case DOOM_MUSIC_WAVE_TRIANGLE:
        sample = phase < UINT16_C(32768)
            ? -INT32_C(32768) + (int32_t)phase * INT32_C(2)
            : INT32_C(98303) - (int32_t)phase * INT32_C(2);
        break;
    case DOOM_MUSIC_WAVE_SAW:
        sample = (int32_t)(int16_t)phase;
        break;
    case DOOM_MUSIC_WAVE_SQUARE:
        sample = (voice->phase & UINT32_C(0x80000000)) == 0U
            ? INT32_C(32767) : -INT32_C(32768);
        break;
    case DOOM_MUSIC_WAVE_NOISE:
        player->noise_state ^= player->noise_state << 13U;
        player->noise_state ^= player->noise_state >> 17U;
        player->noise_state ^= player->noise_state << 5U;
        sample = (int32_t)(int16_t)(player->noise_state >> 16U);
        break;
    default:
        break;
    }
    voice->phase += voice->phase_step;
    return sample;
}

static void update_envelope(doom_music_voice_t *voice)
{
    if (voice->releasing || voice->percussion) {
        const uint16_t step = voice->percussion
            ? DOOM_MUSIC_PERCUSSION_DECAY : DOOM_MUSIC_RELEASE_STEP;
        if (voice->envelope_q15 <= step) {
            memset(voice, 0, sizeof(*voice));
        } else {
            voice->envelope_q15 = (uint16_t)(voice->envelope_q15 - step);
        }
    } else if (voice->envelope_q15 < DOOM_MUSIC_ENVELOPE_MAX) {
        const uint32_t next =
            (uint32_t)voice->envelope_q15 + DOOM_MUSIC_ATTACK_STEP;
        voice->envelope_q15 = next > DOOM_MUSIC_ENVELOPE_MAX
            ? DOOM_MUSIC_ENVELOPE_MAX : (uint16_t)next;
    }
}

static void refresh_voice_gains(doom_music_player_t *player)
{
    for (size_t index = 0U; index < (size_t)DOOM_MUSIC_MAX_VOICES; ++index) {
        doom_music_voice_t *const voice = &player->voices[index];
        if (!voice->active) {
            continue;
        }
        const doom_music_channel_t *const channel =
            &player->channels[voice->channel];
        uint64_t gain = (uint64_t)DOOM_MUSIC_ENVELOPE_MAX *
            (uint64_t)voice->velocity * (uint64_t)channel->volume *
            (uint64_t)channel->expression * (uint64_t)player->volume;
        gain /= UINT64_C(127) * UINT64_C(127) * UINT64_C(127) *
                UINT64_C(127) * (uint64_t)DOOM_MUSIC_VOICE_DIVISOR;
        const uint32_t pan = (uint32_t)channel->pan;
        const uint32_t left_pan = pan <= UINT32_C(64)
            ? UINT32_C(127) : (UINT32_C(127) - pan) * UINT32_C(2);
        const uint32_t right_pan = pan >= UINT32_C(64)
            ? UINT32_C(127) : pan * UINT32_C(2);
        voice->left_gain_q15 = (uint16_t)(
            (gain * (uint64_t)left_pan) / UINT64_C(127));
        voice->right_gain_q15 = (uint16_t)(
            (gain * (uint64_t)right_pan) / UINT64_C(127));
    }
    player->gain_dirty = false;
}

static int16_t saturating_add(int16_t existing, int32_t addition)
{
    const int32_t sum = (int32_t)existing + addition;
    if (sum > INT16_MAX) {
        return INT16_MAX;
    }
    if (sum < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)sum;
}

static uint32_t absolute_pcm16(int16_t sample)
{
    return sample == INT16_MIN ? UINT32_C(32768)
                               : (uint32_t)(sample < 0 ? -sample : sample);
}

static uint32_t synthesize_frame(doom_music_player_t *player,
                                 int16_t *left,
                                 int16_t *right)
{
    int16_t music_left = 0;
    int16_t music_right = 0;
    if (player->gain_dirty) {
        refresh_voice_gains(player);
    }
    for (size_t index = 0U; index < (size_t)DOOM_MUSIC_MAX_VOICES; ++index) {
        doom_music_voice_t *const voice = &player->voices[index];
        if (!voice->active) {
            continue;
        }
        int64_t mono = (int64_t)waveform_sample(player, voice);
        mono = (mono * (int64_t)voice->envelope_q15) /
               (int64_t)DOOM_MUSIC_ENVELOPE_MAX;
        const int32_t left_value = (int32_t)(
            (mono * (int64_t)voice->left_gain_q15) /
            (int64_t)DOOM_MUSIC_ENVELOPE_MAX);
        const int32_t right_value = (int32_t)(
            (mono * (int64_t)voice->right_gain_q15) /
            (int64_t)DOOM_MUSIC_ENVELOPE_MAX);
        music_left = saturating_add(music_left, left_value);
        music_right = saturating_add(music_right, right_value);
        update_envelope(voice);
    }
    *left = saturating_add(*left, music_left);
    *right = saturating_add(*right, music_right);
    const uint32_t left_abs = absolute_pcm16(music_left);
    const uint32_t right_abs = absolute_pcm16(music_right);
    return left_abs > right_abs ? left_abs : right_abs;
}

bool doom_music_player_mix(doom_music_player_t *player,
                           int16_t *interleaved_pcm,
                           size_t frame_count)
{
    if (player == NULL || interleaved_pcm == NULL || frame_count == 0U ||
        frame_count > SIZE_MAX / (size_t)DOOM_AUDIO_CHANNEL_COUNT) {
        return false;
    }
    for (size_t frame = 0U; frame < frame_count; ++frame) {
        if (player->playing && !player->paused) {
            (void)process_events(player);
            if (player->playing) {
                int16_t *const left =
                    &interleaved_pcm[frame * (size_t)DOOM_AUDIO_CHANNEL_COUNT];
                int16_t *const right = &interleaved_pcm[
                    frame * (size_t)DOOM_AUDIO_CHANNEL_COUNT + 1U];
                const uint32_t peak = synthesize_frame(player, left, right);
                if (player->samples_until_event > 0U) {
                    --player->samples_until_event;
                }
                ++player->stats.mixed_frames;
                if (peak > player->stats.maximum_absolute_mix) {
                    player->stats.maximum_absolute_mix = peak;
                }
            }
        }
    }
    player->stats.playing = player->playing;
    player->stats.paused = player->paused;
    return true;
}

void doom_music_player_get_stats(const doom_music_player_t *player,
                                 doom_music_stats_t *out_stats)
{
    if (player != NULL && out_stats != NULL) {
        *out_stats = player->stats;
        out_stats->playing = player->playing;
        out_stats->paused = player->paused;
    }
}
