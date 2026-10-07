// SPDX-License-Identifier: MIT
// Original effects and electronic arrangement of Bach (public-domain score).
#include "blast_circuit_internal.h"
#include "generated/audio.inc"
#include "generated/music.inc"

void bc_cue(bc_state_t *s,bc_sound_t sound)
{ if((unsigned)sound<BC_SOUNDS) s->cues|=UINT32_C(1)<<(unsigned)sound; }
static int sample(bc_audio_t *a,unsigned voice)
{
    static const int steps[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,
        50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,
        449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,
        2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,
        11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
    static const int changes[8]={-1,-1,-1,-1,2,4,6,8};
    const unsigned t=bc_sound_specs[voice].frames-a->effect_left[voice];
    const unsigned byte=bc_sound_data[bc_sound_specs[voice].offset+t/2U];
    const unsigned code=(byte>>((t%2U)*4U))&15U;
    const int step=steps[a->adpcm_index[voice]];
    int delta=step>>3;
    if(code&4U) delta+=step;
    if(code&2U) delta+=step>>1;
    if(code&1U) delta+=step>>2;
    int pred=a->filter[voice]+((code&8U)?-delta:delta);
    if(pred>32767) pred=32767;
    if(pred< -32768) pred= -32768;
    int index=(int)a->adpcm_index[voice]+changes[code&7U];
    if(index<0) index=0;
    if(index>88) index=88;
    a->adpcm_index[voice]=(uint8_t)index;a->filter[voice]=pred;--a->effect_left[voice];
    return pred;
}
// Five monophonic score channels and four fixed percussion voices. Timing is
// sample-based, so OS update sizes and FIFO rejection cannot change the tempo.
static void music_tick(bc_audio_t *a)
{
    const bc_track_t *track=&bc_tracks[a->music_track];
    while(a->music_event<track->count && track->score[a->music_event].tick==a->music_tick) {
        const bc_note_t *note=&track->score[a->music_event++];
        bc_music_voice_t *v=&a->music[note->voice];
        v->phase=0U;v->age=0U;v->frames=(uint16_t)(note->duration*track->tick_frames);
        v->step=bc_note_step[note->note];v->velocity=note->velocity;
    }
    if(track->style==0U) {
        // Preserve the approved Badinerie arrangement and its pickup/backbeat.
        const unsigned step=(a->music_tick+8U)%32U;
        const bool bridge=a->music_tick>=512U && a->music_tick<640U;
        const bool fill=(a->music_tick>=248U && a->music_tick<256U) ||
                        (a->music_tick>=504U && a->music_tick<512U) ||
                        (a->music_tick>=888U && a->music_tick<896U) || a->music_tick>=1272U;
        if(step%8U==0U && (!bridge || step%16U==0U)) a->drum_left[0]=bc_drum_specs[0].frames;
        if(step==8U || step==24U || (fill && step%4U==0U)) a->drum_left[1]=bc_drum_specs[1].frames;
        if(step%4U==0U && !bridge) a->drum_left[2]=bc_drum_specs[2].frames;
        if(step==28U && !bridge) a->drum_left[3]=bc_drum_specs[3].frames;
    } else if(track->style==1U) {
        // A syncopated three-beat groove follows the invention's written meter.
        const unsigned step=a->music_tick%24U;
        const bool second=a->music_tick>=track->phrase_ticks;
        if(step==0U || step==12U) a->drum_left[0]=bc_drum_specs[0].frames;
        if(step==8U || (second && step==20U)) a->drum_left[1]=bc_drum_specs[1].frames;
        if(step%4U==0U) a->drum_left[2]=bc_drum_specs[2].frames;
        if(second && step==20U) a->drum_left[3]=bc_drum_specs[3].frames;
    } else {
        // Four-on-the-floor drive; the second pass starts with a short break.
        const unsigned step=a->music_tick%32U,phrase=a->music_tick%track->phrase_ticks;
        const bool break_down=a->music_tick>=track->phrase_ticks && phrase<128U;
        const bool fill=phrase>=track->phrase_ticks-8U;
        if(step%(break_down?16U:8U)==0U) a->drum_left[0]=bc_drum_specs[0].frames;
        if(step==8U || step==24U || (fill && step%4U==0U)) a->drum_left[1]=bc_drum_specs[1].frames;
        if(!break_down && step%4U==0U) a->drum_left[2]=bc_drum_specs[2].frames;
        if(!break_down && step==28U) a->drum_left[3]=bc_drum_specs[3].frames;
    }
}
// All callers pass bounded mixer products, so negation cannot reach INT_MIN.
// Keep signed division's truncation toward zero while avoiding an RV32 DIV.
static inline __attribute__((always_inline)) int signed_shift(int value,unsigned bits)
{
    return value<0?-(int)((unsigned)(-value)>>bits):(int)((unsigned)value>>bits);
}
static int music_voice(bc_music_voice_t *v,unsigned voice,unsigned theme)
{
    if(v->age>=v->frames) return 0;
    const unsigned age=v->age++,remaining=(unsigned)v->frames-age;
    unsigned envelope=age<80U?age*1024U/80U:1024U;
    if(remaining<240U) envelope=envelope*remaining/240U;
    // Rounded pluck; no clicks at note replacement or release. Bass is fuller,
    // while offbeat keys decay faster and leave space for the melody/effects.
    const unsigned decay=1024U-age*512U/(unsigned)v->frames;
    envelope=envelope*decay/1024U;
    if(voice>=2U) envelope=envelope*decay/1024U;
    const int16_t *wave=voice==1U?bc_wave_bass:voice>=2U?bc_wave_keys:
                        theme==2U?bc_wave_glass_lead:bc_wave_lead;
    const unsigned index=(v->phase>>8U)&255U,fraction=v->phase&255U;
    const int value=signed_shift((int)wave[index]*(int)(256U-fraction)+(int)wave[(index+1U)&255U]*(int)fraction,8U);
    v->phase=(v->phase+v->step)&65535U;
    return signed_shift(signed_shift(value*(int)envelope,10U)*(int)v->velocity,voice>=2U?10U:9U);
}
static void music_sample(bc_audio_t *a,unsigned theme,int *left,int *right)
{
    if(a->music_frame==0U) music_tick(a);
    const int lead=music_voice(&a->music[0],0U,theme);
    const int bass=music_voice(&a->music[1],1U,theme);
    const int key1=music_voice(&a->music[2],2U,theme);
    const int key2=music_voice(&a->music[3],3U,theme);
    const int key3=music_voice(&a->music[4],4U,theme);
    int percussion=0;
    for(unsigned d=0U;d<4U;++d) if(a->drum_left[d]) {
        const unsigned offset=(unsigned)bc_drum_specs[d].frames-a->drum_left[d]--;
        percussion+=bc_drum_data[bc_drum_specs[d].offset+offset];
    }
    const int echo=a->delay[a->delay_at];
    unsigned side=(unsigned)a->delay_at+BC_MUSIC_DELAY/2U;
    if(side>=BC_MUSIC_DELAY) side-=BC_MUSIC_DELAY;
    const int early=a->delay[side];
    a->delay[a->delay_at]=(int16_t)(lead/3+signed_shift(echo,2U));
    a->delay_at=(uint16_t)((unsigned)a->delay_at+1U);
    if(a->delay_at==BC_MUSIC_DELAY) a->delay_at=0U;
    *left=lead+bass+key1+signed_shift(key2,1U)+key3/3+percussion+signed_shift(early,1U);
    *right=lead+bass+key1/3+signed_shift(key2,1U)+key3+percussion+signed_shift(echo,1U);
    const bc_track_t *track=&bc_tracks[a->music_track];
    const uint32_t position=(uint32_t)a->music_tick*track->tick_frames+a->music_frame;
    const uint32_t total=(uint32_t)track->ticks*track->tick_frames;
    // End on exactly zero before changing key/tempo and clearing old echoes.
    // Effects remain independent of these short (100-ms) musical transitions.
    uint32_t fade=1600U;
    if(position<fade) fade=position;
    if(total-position-1U<fade) fade=total-position-1U;
    if(fade!=1600U) {
        *left=*left*(int)fade/1600;*right=*right*(int)fade/1600;
    }
    if(++a->music_frame==track->tick_frames) {
        a->music_frame=0U;
        if(++a->music_tick==track->ticks) {
            a->music_tick=0U;a->music_event=0U;
            a->music_track=(uint8_t)(((unsigned)a->music_track+1U)%BC_MUSIC_TRACKS);
            for(unsigned v=0U;v<BC_MUSIC_VOICES;++v) a->music[v]=(bc_music_voice_t){0};
            for(unsigned d=0U;d<4U;++d) a->drum_left[d]=0U;
            for(unsigned i=0U;i<BC_MUSIC_DELAY;++i) a->delay[i]=0;
            a->delay_at=0U;
        }
    }
}
static int limit(int value)
{
    const bool negative=value<0;
    unsigned magnitude=(unsigned)(negative?-value:value);
    if(magnitude>24000U) magnitude=30000U-36000000U/(magnitude-18000U);
    return negative?-(int)magnitude:(int)magnitude;
}
void bc_audio_update(p4_game_context_t *c,bc_state_t *s,uint32_t elapsed)
{
    bc_audio_t *a=&s->audio;
    if(s->events&BC_EVENT_BOMB) bc_cue(s,BC_S_PLACE);
    if(s->events&BC_EVENT_BLAST) bc_cue(s,BC_S_BLAST);
    s->events=0U;
    const uint32_t cues=s->cues;s->cues=0U;
    if(a->muted) {
        for(unsigned i=0;i<BC_SOUNDS;++i) a->effect_left[i]=0U;
        a->music_gain=0;return;
    }
    if(!c->services || !(c->services->available_capabilities&P4_GAME_CAP_AUDIO_STREAM)) {
        // A short bounded fallback retains distinct pitches when PCM is absent.
        for(unsigned i=BC_SOUNDS;i>0U;--i) if(cues&(1U<<(i-1U))) {
            const unsigned hz=i==BC_S_BLAST+1U?65U:180U+(i*67U)%850U;
            (void)p4_game_play_tone(c,(uint16_t)hz,i==BC_S_BLAST+1U?130U:60U,3U,P4_WAVE_TRIANGLE);break;
        }
        return;
    }
    for(unsigned i=0;i<BC_SOUNDS;++i) if(cues&(1U<<i)) {
        // Footfalls/rapid UI repeats never restart a still-ringing impact.
        if((i==BC_S_STEP || i==BC_S_STEP_METAL || i==BC_S_STEP_GLASS || i==BC_S_NAV) && a->effect_left[i]) continue;
        a->effect_left[i]=bc_sound_specs[i].frames;a->adpcm_index[i]=0U;a->filter[i]=0;
    }
    uint8_t active[BC_SOUNDS];unsigned active_count=0U;
    for(unsigned i=0;i<BC_SOUNDS;++i) if(a->effect_left[i]) active[active_count++]=(uint8_t)i;
    const bool quiet=s->world.phase==BC_PAUSED || s->world.phase==BC_LOST || s->world.phase==BC_WAIT || s->world.phase==BC_TRANSFER;
    const bool battle=s->world.phase==BC_PLAY || s->world.phase==BC_READY;
    const int target=quiet?0:battle?128:76;
    const unsigned theme=s->world.theme%BC_THEMES;
    unsigned remaining=elapsed*16U;
    while(remaining) {
        const unsigned frames=remaining>256U?256U:remaining;
        int16_t block[512];
        for(unsigned i=0;i<frames;++i) {
            if(a->music_gain<target) ++a->music_gain;
            else if(a->music_gain>target) --a->music_gain;
            int left,right;
            music_sample(a,theme,&left,&right);
            if(a->music_gain!=128) {
                left=signed_shift(left*a->music_gain,7U);right=signed_shift(right*a->music_gain,7U);
            }
            int effects=0;
            for(unsigned n=0;n<active_count;++n) {
                const unsigned voice=active[n];
                if(a->effect_left[voice]) effects+=sample(a,voice);
            }
            left+=effects;right+=effects;
            block[i*2U]=(int16_t)limit(left);block[i*2U+1U]=(int16_t)limit(right);++a->clock;
        }
        (void)p4_game_submit_pcm16_stereo(c,block,frames);remaining-=frames;
    }
}
