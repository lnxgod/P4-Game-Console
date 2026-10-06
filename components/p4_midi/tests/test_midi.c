// SPDX-License-Identifier: GPL-2.0-or-later
#include "p4/midi.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static const uint8_t song[]={
 'M','T','h','d',0,0,0,6,0,0,0,1,0,96,
 'M','T','r','k',0,0,0,23,
 0,0xc0,0, 0,0x90,60,100, 96,60,0,
 0,0xff,0x51,3,0x0f,0x42,0x40,
 96,0x90,64,100, 0,0xff,0x2f,0
};
int main(void) {
 uint8_t data[sizeof(song)];memcpy(data,song,sizeof(data));data[21]=(uint8_t)(sizeof(song)-22);
 p4_midi_player_t p;
 assert(p4_midi_start(&p,data,sizeof(data),true));
 int16_t pcm[512];uint64_t power=0;
 for(unsigned i=0;i<125;++i){memset(pcm,0,sizeof(pcm));assert(p4_midi_mix(&p,pcm,256));for(unsigned j=0;j<512;++j)power+=(uint64_t)(pcm[j]*(int32_t)pcm[j]);}
 assert(power>0&&p.stats.loops_completed==1&&p.stats.notes_started>=3);
 p4_midi_stop(&p);memset(pcm,0,sizeof(pcm));assert(p4_midi_mix(&p,pcm,256));
 for(unsigned i=0;i<512;++i)assert(!pcm[i]);
 assert(!p4_midi_mix(&p,pcm,257));
 for(size_t n=0;n<sizeof(data);++n)assert(!p4_midi_start(&p,data,n,false));
 // Invalid data byte, unterminated VLQ, invalid division and running status.
 data[28]=0xff;assert(p4_midi_start(&p,data,sizeof(data),false));assert(!p4_midi_mix(&p,pcm,256));
 memcpy(data,song,sizeof(data));data[21]=(uint8_t)(sizeof(song)-22);memset(data+22,0x80,4);
 assert(!p4_midi_start(&p,data,sizeof(data),false));
 memcpy(data,song,sizeof(data));data[12]=0x80;assert(!p4_midi_start(&p,data,sizeof(data),false));
 puts("PASS: audible MIDI, tempo changes, running status, looping, truncation and malformed events");
}
