/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WACKY_SOUND_H
#define WACKY_SOUND_H
#include "p4/midi.h"
#include "p4/game.h"
#include "ww_archive.h"
typedef struct { const uint8_t *samples; uint32_t count, rate; bool loop; } WwSample;
typedef struct { WwSample sample; uint32_t position, step; uint8_t volume; bool active; } WwVoice;
typedef struct {
 p4_midi_player_t music;
 WwVoice voices[8];
 uint8_t music_volume, effects_volume, engine_volume;
 uint32_t submitted, rejected;
 bool enabled;
} WwSoundPlayer;
bool ww_sound_sample(const WwArchive *,const char *,WwSample *);
void ww_sound_music(WwSoundPlayer *,const WwArchive *,const char *);
void ww_sound_effect(WwSoundPlayer *,const WwArchive *,const char *);
void ww_sound_engine(WwSoundPlayer *,const WwArchive *,unsigned speed,bool on);
void ww_sound_update(WwSoundPlayer *,p4_game_context_t *,uint32_t ms);
void ww_sound_silence(WwSoundPlayer *);
#endif
