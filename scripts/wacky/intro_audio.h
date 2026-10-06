/* SPDX-License-Identifier: GPL-2.0-or-later
 * The upstream intro's audio interface, backed by the shared P4 PCM service. */
#ifndef WW_AUDIO_H
#define WW_AUDIO_H
#include "sound.h"
#include <string.h>
typedef struct { const WwArchive *archive; char name[15]; } WwSound;
typedef struct { WwSoundPlayer *player; uint8_t music_volume; } WwAudio;
static inline bool ww_audio_load_voc(const WwArchive *a,const char *n,WwSound *s) {
 WwSample sample;if(!ww_sound_sample(a,n,&sample))return false;
 s->archive=a;strncpy(s->name,n,14);s->name[14]=0;return true;
}
static inline void ww_audio_free_sound(WwSound *s){memset(s,0,sizeof(*s));}
static inline int ww_audio_play(WwAudio *a,const WwSound *s,uint8_t v) {
 (void)v;ww_sound_effect(a->player,s->archive,s->name);return 1;
}
static inline void ww_audio_stop(WwAudio *a,int voice) {
 (void)voice;for(unsigned i=1;i<8;++i)a->player->voices[i].active=false;
}
static inline bool ww_audio_play_midi_asset(WwAudio *a,const WwArchive *r,const char *n,bool loop,uint8_t v) {
 (void)loop;a->music_volume=v;ww_sound_music(a->player,r,n);return a->player->music.playing;
}
#endif
