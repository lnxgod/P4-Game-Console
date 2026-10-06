// SPDX-License-Identifier: GPL-2.0-or-later
/* Synthesis adapted from the existing Doom procedural synthesizer.
 * SMF sequencing and sustain handling are shared cartridge services. */
#include "p4/midi.h"
#include <string.h>
#include <limits.h>
#define P4_MIDI_ENVELOPE_MAX 32767U
#define P4_MIDI_ATTACK_STEP 512U
#define P4_MIDI_RELEASE_STEP 32U
#define P4_MIDI_PERCUSSION_DECAY 16U
#define P4_MIDI_VOICE_DIVISOR 12
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
        uint32_t d=target-base, b=(uint32_t)bend-128u;
        return base+(d/127u)*b+((d%127u)*b)/127u;
    }
    const uint8_t target_note = note < UINT8_C(2)
        ? UINT8_C(0) : (uint8_t)(note - UINT8_C(2));
    const uint32_t target = note_phase_step(target_note);
    return base - (uint32_t)(((uint64_t)(base - target) *
                              (uint64_t)(UINT8_C(128) - bend)) /
                             UINT64_C(128));
}

static p4_midi_waveform_t waveform_for_program(uint8_t program)
{
    if (program < UINT8_C(8)) {
        return P4_MIDI_WAVE_SINE;
    }
    if (program < UINT8_C(24)) {
        return P4_MIDI_WAVE_SQUARE;
    }
    if (program < UINT8_C(40)) {
        return P4_MIDI_WAVE_SAW;
    }
    if (program < UINT8_C(56)) {
        return P4_MIDI_WAVE_TRIANGLE;
    }
    if (program < UINT8_C(80)) {
        return P4_MIDI_WAVE_SAW;
    }
    if (program < UINT8_C(104)) {
        return P4_MIDI_WAVE_SINE;
    }
    return P4_MIDI_WAVE_SQUARE;
}

static p4_midi_voice_t *allocate_voice(p4_midi_player_t *player,
                                          uint8_t channel,
                                          uint8_t note)
{
    p4_midi_voice_t *candidate = NULL;
    for (size_t index = 0U; index < (size_t)P4_MIDI_MAX_VOICES; ++index) {
        p4_midi_voice_t *const voice = &player->voices[index];
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

static void note_on(p4_midi_player_t *player,
                    uint8_t channel,
                    uint8_t note,
                    uint8_t velocity)
{
    p4_midi_voice_t *const voice = allocate_voice(player, channel, note);
    if (voice == NULL) {
        return;
    }
    const bool percussion = channel == UINT8_C(9);
    const p4_midi_channel_t *const channel_state =
        &player->channels[channel];
    *voice = (p4_midi_voice_t){
        .phase = 0U,
        .phase_step = bent_phase_step(note, channel_state->bend),
        .age = ++player->voice_age,
        .envelope_q15 = percussion ? P4_MIDI_ENVELOPE_MAX : 0U,
        .channel = channel,
        .note = note,
        .velocity = velocity,
        .waveform = percussion ? P4_MIDI_WAVE_NOISE
                               : waveform_for_program(channel_state->program),
        .active = true,
        .releasing = false,
        .percussion = percussion,
    };
    player->gain_dirty = true;
    ++player->stats.notes_started;
}

static void note_off(p4_midi_player_t *player,
                     uint8_t channel,
                     uint8_t note)
{
    for (size_t index = 0U; index < (size_t)P4_MIDI_MAX_VOICES; ++index) {
        p4_midi_voice_t *const voice = &player->voices[index];
        if (voice->active && voice->channel == channel && voice->note == note) {
            voice->sustained = player->sustain[channel];
            voice->releasing = !voice->sustained;
        }
    }
}

static void all_notes_off(p4_midi_player_t *player, uint8_t channel,
                          bool immediate)
{
    for (size_t index = 0U; index < (size_t)P4_MIDI_MAX_VOICES; ++index) {
        p4_midi_voice_t *const voice = &player->voices[index];
        if (voice->active && voice->channel == channel) {
            if (immediate) {
                memset(voice, 0, sizeof(*voice));
            } else {
                voice->releasing = true;
            }
        }
    }
}

static void update_channel_pitch(p4_midi_player_t *player, uint8_t channel)
{
    const uint8_t bend = player->channels[channel].bend;
    for (size_t index = 0U; index < (size_t)P4_MIDI_MAX_VOICES; ++index) {
        p4_midi_voice_t *const voice = &player->voices[index];
        if (voice->active && voice->channel == channel && !voice->percussion) {
            voice->phase_step = bent_phase_step(voice->note, bend);
        }
    }
}

static void reset_channel(p4_midi_player_t *player, uint8_t channel)
{
    player->channels[channel] = (p4_midi_channel_t){
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

static int32_t waveform_sample(p4_midi_player_t *player,
                               p4_midi_voice_t *voice)
{
    int32_t sample = 0;
    const uint16_t phase = (uint16_t)(voice->phase >> 16U);
    switch (voice->waveform) {
    case P4_MIDI_WAVE_SINE: {
        const int32_t x = (int32_t)(int16_t)phase;
        const int32_t magnitude = x < 0 ? -x : x;
        sample = (x * (32768 - magnitude)) / 8192;
        break;
    }
    case P4_MIDI_WAVE_TRIANGLE:
        sample = phase < UINT16_C(32768)
            ? -INT32_C(32768) + (int32_t)phase * INT32_C(2)
            : INT32_C(98303) - (int32_t)phase * INT32_C(2);
        break;
    case P4_MIDI_WAVE_SAW:
        sample = (int32_t)(int16_t)phase;
        break;
    case P4_MIDI_WAVE_SQUARE:
        sample = (voice->phase & UINT32_C(0x80000000)) == 0U
            ? INT32_C(32767) : -INT32_C(32768);
        break;
    case P4_MIDI_WAVE_NOISE:
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

static void update_envelope(p4_midi_voice_t *voice)
{
    if (voice->releasing || voice->percussion) {
        const uint16_t step = voice->percussion
            ? P4_MIDI_PERCUSSION_DECAY : P4_MIDI_RELEASE_STEP;
        if (voice->envelope_q15 <= step) {
            memset(voice, 0, sizeof(*voice));
        } else {
            voice->envelope_q15 = (uint16_t)(voice->envelope_q15 - step);
        }
    } else if (voice->envelope_q15 < P4_MIDI_ENVELOPE_MAX) {
        const uint32_t next =
            (uint32_t)voice->envelope_q15 + P4_MIDI_ATTACK_STEP;
        voice->envelope_q15 = next > P4_MIDI_ENVELOPE_MAX
            ? P4_MIDI_ENVELOPE_MAX : (uint16_t)next;
    }
}

static void refresh_voice_gains(p4_midi_player_t *player)
{
    for (size_t index = 0U; index < (size_t)P4_MIDI_MAX_VOICES; ++index) {
        p4_midi_voice_t *const voice = &player->voices[index];
        if (!voice->active) {
            continue;
        }
        const p4_midi_channel_t *const channel =
            &player->channels[voice->channel];
        uint32_t gain=P4_MIDI_ENVELOPE_MAX;
        gain=gain*voice->velocity/127u;
        gain=gain*channel->volume/127u;
        gain=gain*channel->expression/127u;
        gain=gain*player->volume/(127u*P4_MIDI_VOICE_DIVISOR);
        const uint32_t pan = (uint32_t)channel->pan;
        const uint32_t left_pan = pan <= UINT32_C(64)
            ? UINT32_C(127) : (UINT32_C(127) - pan) * UINT32_C(2);
        const uint32_t right_pan = pan >= UINT32_C(64)
            ? UINT32_C(127) : pan * UINT32_C(2);
        voice->left_gain_q15 = (uint16_t)(
            (gain * left_pan) / 127u);
        voice->right_gain_q15 = (uint16_t)(
            (gain * right_pan) / 127u);
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

static uint32_t synthesize_frame(p4_midi_player_t *player,
                                 int16_t *left,
                                 int16_t *right)
{
    int16_t music_left = 0;
    int16_t music_right = 0;
    if (player->gain_dirty) {
        refresh_voice_gains(player);
    }
    for (size_t index = 0U; index < (size_t)P4_MIDI_MAX_VOICES; ++index) {
        p4_midi_voice_t *const voice = &player->voices[index];
        if (!voice->active) {
            continue;
        }
        int32_t mono = waveform_sample(player, voice);
        mono = (mono * voice->envelope_q15) / 32768;
        const int32_t left_value = (mono * voice->left_gain_q15) / 32768;
        const int32_t right_value = (mono * voice->right_gain_q15) / 32768;
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


static uint16_t be16(const uint8_t *b) { return (uint16_t)((uint16_t)b[0]<<8|b[1]); }
static uint32_t be32(const uint8_t *b) { return (uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3]; }
static bool byte(p4_midi_player_t *p,p4_midi_track_t *t,uint8_t *v)
{ if(t->cursor>=t->end)return false;*v=p->data[t->cursor++];return true; }
static bool vlq(p4_midi_player_t *p,p4_midi_track_t *t,uint32_t *out)
{
    uint32_t v=0;uint8_t b;
    for(unsigned i=0;i<4;++i) {
        if(!byte(p,t,&b))return false;
        v=(v<<7)|(b&127);
        if(!(b&128)){*out=v;return true;}
    }
    return false;
}
static bool delta(p4_midi_player_t *p,p4_midi_track_t *t)
{
    uint32_t d;
    if(!vlq(p,t,&d)||d>0xffffffu||t->next_tick>0x7fffffffu-d)return false;
    t->next_tick+=d;return true;
}
static bool rewind_song(p4_midi_player_t *p)
{
    memset(p->voices,0,sizeof(p->voices));memset(p->sustain,0,sizeof(p->sustain));
    for(unsigned i=0;i<16;++i)reset_channel(p,(uint8_t)i);
    p->tick=0;p->tempo=500000;p->samples_until_event=0;p->timing_remainder=0;
    for(unsigned i=0;i<p->track_count;++i) {
        p4_midi_track_t *t=&p->tracks[i];t->cursor=t->begin;t->next_tick=0;
        t->running_status=0;t->ended=false;
        if(!delta(p,t))return false;
    }
    return true;
}
void p4_midi_stop(p4_midi_player_t *p)
{ if(p){p->playing=false;memset(p->voices,0,sizeof(p->voices));} }
void p4_midi_volume(p4_midi_player_t *p,uint8_t volume)
{ if(p){p->volume=volume>127?127:volume;p->gain_dirty=true;} }
bool p4_midi_start(p4_midi_player_t *p,const void *data,size_t bytes,bool loop)
{
    if(!p)return false;
    memset(p,0,sizeof(*p));
    const uint8_t *b=data;
    if(!b||bytes<14||bytes>P4_MIDI_MAX_BYTES||memcmp(b,"MThd",4))return false;
    uint32_t header=be32(b+4);
    if(header<6||header>bytes-8||be16(b+8)>1)return false;
    p->track_count=be16(b+10);p->division=be16(b+12);
    if(!p->track_count||p->track_count>P4_MIDI_MAX_TRACKS||!p->division||
       (p->division&0x8000)||(!be16(b+8)&&p->track_count!=1))return false;
    size_t pos=8+header;
    for(unsigned i=0;i<p->track_count;++i) {
        if(pos>bytes||bytes-pos<8||memcmp(b+pos,"MTrk",4))return false;
        uint32_t n=be32(b+pos+4);pos+=8;
        if(!n||n>bytes-pos)return false;
        p->tracks[i].begin=pos;p->tracks[i].end=pos+n;pos+=n;
    }
    p->data=b;p->bytes=bytes;p->looping=loop;p->volume=100;p->noise_state=0x4d494449;
    if(!rewind_song(p)){p4_midi_stop(p);return false;}
    p->playing=true;p->stats.songs_started=1;return true;
}
static void controller(p4_midi_player_t *p,uint8_t c,uint8_t kind,uint8_t v)
{
    p4_midi_channel_t *s=&p->channels[c];
    switch(kind) {
    case 7:s->volume=v;break;
    case 10:s->pan=v;break;
    case 11:s->expression=v;break;
    case 64:
        p->sustain[c]=v>=64;
        if(v<64)for(unsigned i=0;i<P4_MIDI_MAX_VOICES;++i) {
            p4_midi_voice_t *voice=&p->voices[i];
            if(voice->channel==c&&voice->sustained){voice->sustained=false;voice->releasing=true;}
        }
        break;
    case 120:all_notes_off(p,c,true);break;
    case 121:reset_channel(p,c);p->sustain[c]=false;break;
    case 123:all_notes_off(p,c,false);break;
    default:break;
    }
    p->gain_dirty=true;
}
static bool event(p4_midi_player_t *p,p4_midi_track_t *t)
{
    uint8_t s,a=0,b=0;
    if(!byte(p,t,&s))return false;
    if(s<128){if(!t->running_status)return false;--t->cursor;s=t->running_status;}
    if(s<0xf0) {
        t->running_status=s;
        if(!byte(p,t,&a)||a>127)return false;
        if((s&0xe0)!=0xc0&&(!byte(p,t,&b)||b>127))return false;
        uint8_t c=s&15;
        switch(s>>4) {
        case 8:note_off(p,c,a);break;
        case 9:if(b)note_on(p,c,a,b);else note_off(p,c,a);break;
        case 11:controller(p,c,a,b);break;
        case 12:p->channels[c].program=a;break;
        case 14:p->channels[c].bend=(uint8_t)(((unsigned)b*128+a)>>6);update_channel_pitch(p,c);break;
        default:break;
        }
    } else if(s==0xff) {
        uint32_t n;
        if(!byte(p,t,&a)||!vlq(p,t,&n)||n>t->end-t->cursor)return false;
        if(a==0x2f){if(n)return false;t->ended=true;}
        else if(a==0x51) {
            if(n!=3)return false;
            const uint8_t *v=p->data+t->cursor;
            uint32_t tempo=(uint32_t)v[0]<<16|(uint32_t)v[1]<<8|v[2];
            if(!tempo)return false;p->tempo=tempo;
        }
        t->cursor+=n;
    } else if(s==0xf0||s==0xf7) {
        uint32_t n;t->running_status=0;
        if(!vlq(p,t,&n)||n>t->end-t->cursor)return false;
        t->cursor+=n;
    } else return false;
    ++p->stats.events_processed;
    return t->ended||delta(p,t);
}
/* Bounded division avoids the pinned RV32 libgcc's non-PIC 64-bit divider.
 * This runs only at MIDI event boundaries, never once per audio voice/sample. */
static uint64_t divide_u64(uint64_t numerator,uint64_t denominator,uint64_t *remainder)
{
    uint64_t quotient=0,rest=0;
    for(unsigned i=64;i-- >0;) {
        rest=(rest<<1)|((numerator>>i)&1u);
        if(rest>=denominator){rest-=denominator;quotient|=UINT64_C(1)<<i;}
    }
    *remainder=rest;return quotient;
}
static bool advance(p4_midi_player_t *p,unsigned *budget)
{
    while(!p->samples_until_event&&p->playing) {
        if(!*budget)return false;
        --*budget;
        uint32_t next=UINT32_MAX;unsigned chosen=0;
        for(unsigned i=0;i<p->track_count;++i)
            if(!p->tracks[i].ended&&p->tracks[i].next_tick<next){next=p->tracks[i].next_tick;chosen=i;}
        if(next==UINT32_MAX) {
            if(!p->looping){p4_midi_stop(p);return true;}
            if(!p->tick||!rewind_song(p))return false;
            ++p->stats.loops_completed;continue;
        }
        if(next<p->tick)return false;
        if(next>p->tick) {
            uint32_t d=next-p->tick;
            if(d>0xffffffu)return false;
            uint64_t denominator=(uint64_t)p->division*1000000;
            uint64_t n=(uint64_t)d*p->tempo*P4_MIDI_SAMPLE_RATE+p->timing_remainder;
            uint64_t samples=divide_u64(n,denominator,&p->timing_remainder);
            if(samples>UINT32_MAX)return false;
            p->samples_until_event=(uint32_t)samples;p->tick=next;
        } else if(!event(p,&p->tracks[chosen]))return false;
    }
    return true;
}
bool p4_midi_mix(p4_midi_player_t *p,int16_t *pcm,size_t frames)
{
    if(!p||!pcm||!frames||frames>256)return false;
    unsigned budget=4096;
    for(size_t i=0;i<frames;++i) {
        if(!advance(p,&budget)){++p->stats.parse_failures;p4_midi_stop(p);return false;}
        if(!p->playing)break;
        uint32_t peak=synthesize_frame(p,&pcm[2*i],&pcm[2*i+1]);
        if(peak>p->stats.maximum_absolute_mix)p->stats.maximum_absolute_mix=peak;
        --p->samples_until_event;++p->stats.mixed_frames;
    }
    p->stats.playing=p->playing;return true;
}
