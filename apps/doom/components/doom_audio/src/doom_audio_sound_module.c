// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Project-owned sound and MUS-music adapter for the pinned doomgeneric engine.
 * The worker mixes both sources; no external MIDI service or hardware is used.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "deh_str.h"
#include "doom/audio_mixer.h"
#include "doom/audio_runtime.h"
#include "doom/music_synth.h"
#include "doomtype.h"
#include "esp_err.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"

#define DOOM_AUDIO_MAX_CACHED_LUMPS ((size_t)128U)
#define DOOM_AUDIO_MAX_TOTAL_CACHE_BYTES ((size_t)(1024U * 1024U))

typedef struct {
    int lump_number;
    doom_audio_sample_t sample;
    uint8_t samples[];
} cached_sound_t;

static bool s_initialized;
static bool s_music_initialized;
static bool s_use_sfx_prefix;
static sfxinfo_t *s_sound_table;
static size_t s_sound_count;
static cached_sound_t *s_cache[DOOM_AUDIO_MAX_CACHED_LUMPS];
static size_t s_cache_count;
static size_t s_cache_bytes;

/* Required configuration variables when pinned i_sound.c enables FEATURE_SOUND. */
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65F;

static bool build_lump_name(const sfxinfo_t *sound,
                            char name[9])
{
    if (sound == NULL || name == NULL) {
        return false;
    }
    const sfxinfo_t *const source = sound->link != NULL ? sound->link : sound;
    const char *const source_name = DEH_String(source->name);
    if (source_name == NULL) {
        return false;
    }

    size_t source_length = 0U;
    while (source_length < sizeof(source->name) &&
           source_name[source_length] != '\0') {
        ++source_length;
    }
    const size_t prefix_length = s_use_sfx_prefix ? 2U : 0U;
    if (source_length == 0U || source_length + prefix_length > 8U) {
        return false;
    }
    size_t offset = 0U;
    if (s_use_sfx_prefix) {
        name[offset++] = 'd';
        name[offset++] = 's';
    }
    memcpy(&name[offset], source_name, source_length);
    name[offset + source_length] = '\0';
    return true;
}

static cached_sound_t *find_cached_sound(int lump_number)
{
    for (size_t index = 0U; index < s_cache_count; ++index) {
        if (s_cache[index] != NULL &&
            s_cache[index]->lump_number == lump_number) {
            return s_cache[index];
        }
    }
    return NULL;
}

static cached_sound_t *cache_sound_lump(int lump_number)
{
    cached_sound_t *cached = find_cached_sound(lump_number);
    if (cached != NULL) {
        return cached;
    }
    if (lump_number < 0 ||
        s_cache_count >= (size_t)DOOM_AUDIO_MAX_CACHED_LUMPS) {
        return NULL;
    }
    const int lump_length = W_LumpLength((unsigned int)lump_number);
    if (lump_length <= 0 ||
        (size_t)lump_length >
            (size_t)DOOM_AUDIO_MAX_DMX_SAMPLE_BYTES + (size_t)8U) {
        return NULL;
    }

    const uint8_t *const lump = W_CacheLumpNum(lump_number, PU_STATIC);
    if (lump == NULL) {
        return NULL;
    }
    doom_audio_sample_t parsed;
    const bool parsed_ok = doom_audio_parse_dmx_lump(
        lump, (size_t)lump_length, &parsed);
    if (!parsed_ok || parsed.sample_count >
                          DOOM_AUDIO_MAX_TOTAL_CACHE_BYTES - s_cache_bytes) {
        W_ReleaseLumpNum(lump_number);
        return NULL;
    }

    cached = malloc(sizeof(*cached) + parsed.sample_count);
    if (cached == NULL) {
        W_ReleaseLumpNum(lump_number);
        return NULL;
    }
    cached->lump_number = lump_number;
    cached->sample = parsed;
    memcpy(cached->samples, parsed.samples, parsed.sample_count);
    cached->sample.samples = cached->samples;
    W_ReleaseLumpNum(lump_number);

    s_cache[s_cache_count++] = cached;
    s_cache_bytes += parsed.sample_count;
    return cached;
}

static void release_cache(void)
{
    if (s_sound_table != NULL) {
        for (size_t index = 0U; index < s_sound_count; ++index) {
            s_sound_table[index].driver_data = NULL;
        }
    }
    for (size_t index = 0U; index < s_cache_count; ++index) {
        free(s_cache[index]);
        s_cache[index] = NULL;
    }
    s_sound_table = NULL;
    s_sound_count = 0U;
    s_cache_count = 0U;
    s_cache_bytes = 0U;
}

static boolean module_init(boolean use_sfx_prefix)
{
    if (s_initialized) {
        return true;
    }
    s_use_sfx_prefix = use_sfx_prefix != false;
    if (doom_audio_runtime_start() != ESP_OK) {
        return false;
    }
    s_initialized = true;
    return true;
}

static void module_shutdown(void)
{
    if (!s_initialized) {
        return;
    }
    s_initialized = false;
    /* A timeout means the worker might still hold immutable sample pointers. */
    if (doom_audio_runtime_stop() == ESP_OK) {
        release_cache();
    }
}

static int module_get_sfx_lump_num(sfxinfo_t *sound)
{
    char name[9];
    return build_lump_name(sound, name) ? W_GetNumForName(name) : -1;
}

static void module_update(void)
{
    /* Mixing and blocking I2S writes happen only on the audio worker. */
}

static void module_update_params(int channel, int volume, int separation)
{
    if (channel < 0 || channel >= DOOM_AUDIO_MAX_VOICES ||
        volume < 0 || volume > 127 || separation < 0 || separation > 254) {
        return;
    }
    (void)doom_audio_runtime_update_voice(
        (size_t)channel, (uint8_t)volume, (uint8_t)separation);
}

static int module_start_sound(sfxinfo_t *sound,
                              int channel,
                              int volume,
                              int separation)
{
    if (!s_initialized || sound == NULL ||
        channel < 0 || channel >= DOOM_AUDIO_MAX_VOICES ||
        volume < 0 || volume > 127 || separation < 0 || separation > 254) {
        return -1;
    }
    cached_sound_t *mutable_cached = sound->driver_data;
    if (mutable_cached == NULL && sound->lumpnum >= 0) {
        mutable_cached = cache_sound_lump(sound->lumpnum);
        sound->driver_data = mutable_cached;
    }
    if (mutable_cached == NULL || !doom_audio_runtime_start_voice(
                                      (size_t)channel,
                                      &mutable_cached->sample,
                                      (uint8_t)volume,
                                      (uint8_t)separation)) {
        return -1;
    }
    return channel;
}

static void module_stop_sound(int channel)
{
    if (channel >= 0 && channel < DOOM_AUDIO_MAX_VOICES) {
        doom_audio_runtime_stop_voice((size_t)channel);
    }
}

static boolean module_sound_is_playing(int channel)
{
    return channel >= 0 && channel < DOOM_AUDIO_MAX_VOICES &&
           doom_audio_runtime_voice_active((size_t)channel);
}

static void module_cache_sounds(sfxinfo_t *sounds, int sound_count)
{
    if (!s_initialized || sounds == NULL || sound_count <= 0 ||
        sound_count > 256) {
        return;
    }
    s_sound_table = sounds;
    s_sound_count = (size_t)sound_count;
    for (size_t index = 0U; index < s_sound_count; ++index) {
        char name[9];
        if (!build_lump_name(&sounds[index], name)) {
            continue;
        }
        const int lump_number = W_CheckNumForName(name);
        if (lump_number < 0) {
            continue;
        }
        sounds[index].driver_data = cache_sound_lump(lump_number);
    }
}

static snddevice_t s_sound_devices[] = {
    SNDDEVICE_SB,
};

sound_module_t DG_sound_module = {
    s_sound_devices,
    (int)(sizeof(s_sound_devices) / sizeof(s_sound_devices[0])),
    module_init,
    module_shutdown,
    module_get_sfx_lump_num,
    module_update,
    module_update_params,
    module_start_sound,
    module_stop_sound,
    module_sound_is_playing,
    module_cache_sounds,
};

static boolean music_init(void)
{
    if (!s_initialized) {
        return false;
    }
    s_music_initialized = true;
    return true;
}

static void music_shutdown(void)
{
    if (s_music_initialized) {
        (void)doom_audio_runtime_music_stop();
        s_music_initialized = false;
    }
}

static void music_set_volume(int volume)
{
    if (s_music_initialized && volume >= 0 && volume <= 127) {
        (void)doom_audio_runtime_music_set_volume((uint8_t)volume);
    }
}

static void *music_register(void *data, int length)
{
    if (!s_music_initialized || data == NULL || length <= 0 ||
        (size_t)length > (size_t)DOOM_MUSIC_MAX_SONG_BYTES) {
        return NULL;
    }
    return doom_music_song_create(data, (size_t)length);
}

static void music_unregister(void *handle)
{
    doom_music_song_release(handle);
}

static void music_play(void *handle, boolean looping)
{
    if (s_music_initialized && handle != NULL) {
        (void)doom_audio_runtime_music_play(
            handle, looping != false);
    }
}

static void music_pause(void)
{
    if (s_music_initialized) {
        (void)doom_audio_runtime_music_pause();
    }
}

static void music_resume(void)
{
    if (s_music_initialized) {
        (void)doom_audio_runtime_music_resume();
    }
}

static void music_stop(void)
{
    if (s_music_initialized) {
        (void)doom_audio_runtime_music_stop();
    }
}

static boolean music_is_playing(void)
{
    return s_music_initialized && doom_audio_runtime_music_is_playing();
}

static snddevice_t s_music_devices[] = {
    SNDDEVICE_SB,
    SNDDEVICE_ADLIB,
    SNDDEVICE_GENMIDI,
};

music_module_t DG_music_module = {
    s_music_devices,
    (int)(sizeof(s_music_devices) / sizeof(s_music_devices[0])),
    music_init,
    music_shutdown,
    music_set_volume,
    music_pause,
    music_resume,
    music_register,
    music_unregister,
    music_play,
    music_stop,
    music_is_playing,
    NULL,
};
