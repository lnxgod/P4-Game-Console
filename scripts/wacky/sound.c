/* SPDX-License-Identifier: GPL-2.0-or-later
 * VOC effects borrow validated archive bytes; no sample allocations. */
#include "sound.h"
#include <string.h>
bool ww_sound_sample(const WwArchive *archive,const char *name,WwSample *out) {
 WwArchiveView view;memset(out,0,sizeof(*out));
 if(!ww_archive_view(archive,name,&view)||view.size<26||
    memcmp(view.data,"Creative Voice File",19)||view.data[19]!=0x1a)return false;
 size_t p=ww_read_le16(view.data+20);if(p<26||p>view.size)return false;
 for(unsigned budget=0;budget<64&&p<view.size;++budget) {
  uint8_t type=view.data[p++];if(!type)return out->count!=0;
  if(view.size-p<3)return false;
  uint32_t n=ww_read_le24(view.data+p);p+=3;if(n>view.size-p)return false;
  if(type==1) {
   if(n<3||view.data[p+1]||out->samples)return false;
   out->samples=view.data+p+2;out->count=n-2;out->rate=1000000u/(256u-view.data[p]);
   if(out->count>65535||out->rate>48000)return false;
  } else if(type==6) { if(n!=2)return false;out->loop=true; }
  else if(type!=7&&type!=4&&type!=5)return false;
  p+=n;
 }
 return false;
}
void ww_sound_music(WwSoundPlayer *s,const WwArchive *a,const char *name) {
 WwArchiveView v;p4_midi_stop(&s->music);
 if(s->enabled&&ww_archive_view(a,name,&v)) {
  (void)p4_midi_start(&s->music,v.data,v.size,true);
  p4_midi_volume(&s->music,(uint8_t)(s->music_volume*127u/10u));
 }
}
void ww_sound_effect(WwSoundPlayer *s,const WwArchive *a,const char *name) {
 if(!s->enabled||!s->effects_volume)return;
 WwSample sample;if(!ww_sound_sample(a,name,&sample))return;
 unsigned v=1;for(unsigned i=1;i<8;++i)if(!s->voices[i].active){v=i;break;}
 s->voices[v]=(WwVoice){.sample=sample,.step=(sample.rate*65536u)/16000u,
  .volume=s->effects_volume,.active=true};
}
void ww_sound_engine(WwSoundPlayer *s,const WwArchive *a,unsigned speed,bool on) {
 WwVoice *v=&s->voices[0];
 if(!s->enabled||!on||!s->engine_volume){v->active=false;return;}
 if(!v->active) {
  WwSample sample;if(!ww_sound_sample(a,"MOTOR.VOC",&sample))return;
  *v=(WwVoice){.sample=sample,.active=true};
 }
 if(speed>100)speed=100;
 v->step=(v->sample.rate*(20000u+speed*500u))/16000u;
 v->volume=s->engine_volume;
}
void ww_sound_silence(WwSoundPlayer *s){memset(s->voices,0,sizeof(s->voices));p4_midi_stop(&s->music);}
void ww_sound_update(WwSoundPlayer *s,p4_game_context_t *ctx,uint32_t ms) {
 if(!s->enabled)return;
 if(ms>100)ms=100;
 unsigned frames=ms*16u;
 p4_midi_volume(&s->music,(uint8_t)(s->music_volume*127u/10u));
 for(unsigned batch=0;batch<7&&frames;++batch) {
  unsigned n=frames>256?256:frames;int16_t pcm[512]={0};
  (void)p4_midi_mix(&s->music,pcm,n);
  for(unsigned f=0;f<n;++f) {
   int32_t mix=0;
   for(unsigned i=0;i<8;++i) {
    WwVoice *v=&s->voices[i];if(!v->active)continue;
    unsigned pos=v->position>>16;
    if(pos>=v->sample.count){v->active=false;continue;}
    mix+=((int32_t)v->sample.samples[pos]-128)*v->volume*12;
    v->position+=v->step;
    if((v->position>>16)>=v->sample.count) {
     if(v->sample.loop)v->position-=(v->sample.count<<16);else v->active=false;
    }
   }
   for(unsigned c=0;c<2;++c){int32_t v=pcm[2*f+c]+mix;pcm[2*f+c]=(int16_t)(v>32767?32767:v< -32768?-32768:v);}
  }
  if(p4_game_submit_pcm16_stereo(ctx,pcm,n))s->submitted+=n;else s->rejected+=n;
  frames-=n;
 }
}
