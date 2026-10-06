#!/usr/bin/env python3
"""Original deterministic sound design. No samples or external audio inputs.

16 kHz, PCM16 masters -> IMA ADPCM cartridge bank. Each voice resets predictor
and index to zero; the C mixer decodes one nibble per frame. --audition writes
a labeled-order WAV with a quarter second between cues for listening review.
"""
from pathlib import Path
import argparse, hashlib, json, math, random, struct, wave
ROOT = Path(__file__).resolve().parents[1]
RATE = 16000
NAMES = ['nav','confirm','place','blast','wood','metal','glass','range','extra',
         'speed','step','death','count','go','round','champion','draw','pause',
         'paint','erase','save','error','transfer','warning','step_metal',
         'step_glass','death_ember','death_volt','death_gilt','sizzle']
DURATIONS = [.06,.18,.16,.66,.28,.40,.42,.30,.32,.30,.055,.48,.17,.38,
             .78,1.38,.46,.16,.07,.12,.38,.16,.5,.42,.065,.06,.48,.48,.52,1.4]
GAINS = [.12,.21,.26,.60,.31,.31,.29,.24,.24,.24,.065,.34,.21,.36,
         .31,.35,.24,.17,.13,.12,.22,.18,.20,.25,.065,.065,.34,.34,.36,.22]
STEP = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,
        50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,
        253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,
        1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,
        3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,
        10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,
        27086,29794,32767]
INDEX = [-1,-1,-1,-1,2,4,6,8]
def sine(hz,t): return math.sin(2*math.pi*hz*t)
def bell(t,hz,decay=.10):
    if t<0: return 0.0
    return (sine(hz,t)+.35*sine(hz*2.71,t)+.12*sine(hz*4.07,t))*math.exp(-t/decay)
def chord(t,notes,decay=.18):
    if t<0: return 0.0
    return sum(bell(t,h,decay) for h in notes)/len(notes)
def design(kind,duration,seed):
    rng=random.Random(seed);low=0.;slow=0.;previous=0.;dc=0.;out=[]
    for i in range(round(duration*RATE)):
        t=i/RATE;n=rng.uniform(-1,1);low += (n-low)*.16;slow += (n-slow)*.035
        if kind=='nav': v=bell(t,780,.013)*.5+n*math.exp(-t/.006)*.2
        elif kind=='confirm': v=chord(t,[523.25,783.99],.060)+.14*bell(t-.045,1046.5,.04)
        elif kind=='place': v=sine(118,t)*math.exp(-t/.032)+.55*bell(t,426,.025)+n*math.exp(-t/.004)*.4
        elif kind=='blast':
            body=math.sin(2*math.pi*(42*t+2.8*(1-math.exp(-t*22))))
            v=body*math.exp(-t/.14)*.65+slow*math.exp(-t/.19)*3.4+low*math.exp(-t/.075)*1.2+n*math.exp(-t/.007)*.32
            v+=.28*bell(t-.045,57,.10)+.14*bell(t-.095,51,.09)
        elif kind=='wood':
            v=0.
            for delay,amp in [(0,1),(.024,.55),(.058,.3),(.094,.12)]:
                z=t-delay
                if z>=0: v+=amp*(.6*bell(z,327,.018)+low*math.exp(-z/.018)*2+n*math.exp(-z/.004)*.2)
        elif kind=='sizzle':
            swell=(1-math.exp(-t*45))*math.exp(-t/.55)
            v=(low-slow)*swell*2.4+slow*math.exp(-t/.15)*1.3
            v+=.08*bell(t,115,.06)
        elif kind=='metal': v=chord(t,[183,497,893,1481],.075)+n*math.exp(-t/.004)*.14
        elif kind=='glass': v=chord(t,[1399,1877,2549,3263],.056)+.3*chord(t-.028,[1801,2903],.09)+n*math.exp(-t/.005)*.2
        elif kind=='range': v=chord(t,[523.25,783.99],.09)+.45*bell(t-.065,1046.5,.08)
        elif kind=='extra': v=bell(t,261.63,.04)+.8*bell(t-.045,523.25,.06)+.55*bell(t-.105,659.25,.065)
        elif kind=='speed': v=(n-low)*math.sin(math.pi*min(1,t/.22))**2*math.exp(-t/.06)+.24*chord(t,[880,1318.51],.09)
        elif kind.startswith('step'):
            h=92 if kind=='step' else 184 if kind=='step_metal' else 980
            v=bell(t,h,.009)*.7+low*math.exp(-t/.008)*.8
        elif kind.startswith('death'):
            h={'death':82,'death_ember':98,'death_volt':123,'death_gilt':65}[kind]
            v=chord(t,[h,h*2.07,h*3],.085)+slow*math.exp(-t/.1)*1.8+(n-low)*math.exp(-t/.014)*.12
        elif kind=='count': v=bell(t,440,.04)+.2*bell(t,880,.025)
        elif kind=='go': v=chord(t,[261.63,523.25,659.25,783.99],.10)+.35*bell(t,65.41,.06)
        elif kind=='round': v=chord(t,[523.25,622.25,783.99],.17)+.6*chord(t-.13,[622.25,783.99,1046.5],.18)
        elif kind=='champion':
            v=chord(t,[261.63,523.25,659.25],.24)+.7*chord(t-.18,[392,659.25,783.99],.19)+.9*chord(t-.44,[523.25,783.99,1046.5],.24)
            v+=.35*bell(t,65.41,.12)+.4*bell(t-.44,130.81,.12)
        elif kind=='draw': v=chord(t,[174.61,261.63],.10)+.5*bell(t-.095,174.61,.07)
        elif kind=='pause': v=chord(t,[349.23,523.25],.035)+low*math.exp(-t/.02)*.1
        elif kind=='paint': v=bell(t,630,.018)+n*math.exp(-t/.003)*.18
        elif kind=='erase': v=low*math.exp(-t/.018)*1.5+bell(t,210,.02)*.25
        elif kind=='save': v=chord(t,[659.25,987.77],.075)+.6*chord(t-.09,[783.99,1318.51],.07)
        elif kind=='error': v=bell(t,146.83,.018)+.7*bell(t-.055,146.83,.018)
        elif kind=='transfer': v=bell(t,523.25,.025)+.6*bell(t-.07,659.25,.035)+.7*chord(t-.14,[783.99,1046.5],.09)
        elif kind=='warning': v=chord(t,[164.81,174.61],.06)+.7*chord(t-.18,[164.81,174.61],.06)
        else: raise ValueError(kind)
        # Gentle DC blocker and finite smooth attack/release avoid clicks.
        dc=v-previous+.995*dc;previous=v
        fade=min(1,t/.0015,(duration-t)/.012)
        out.append(dc*max(0,fade))
    peak=max(abs(v) for v in out) or 1
    return [round(v/peak*GAINS[NAMES.index(kind)]*32767) for v in out]
def encode(samples):
    pred=0;index=0;nibbles=[];decoded=[]
    for target in samples:
        step=STEP[index];diff=target-pred;code=8 if diff<0 else 0;diff=abs(diff);delta=step>>3
        for bit,part in [(4,step),(2,step>>1),(1,step>>2)]:
            if diff>=part: code|=bit;diff-=part;delta+=part
        pred=max(-32768,min(32767,pred+(-delta if code&8 else delta)))
        index=max(0,min(88,index+INDEX[code&7]));nibbles.append(code);decoded.append(pred)
    if len(nibbles)%2: nibbles.append(0)
    return bytes(nibbles[i]|nibbles[i+1]<<4 for i in range(0,len(nibbles),2)),decoded

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--audition',type=Path);args=parser.parse_args()
    bank=bytearray();specs=[];audit=[];audition=[]
    for i,(name,duration) in enumerate(zip(NAMES,DURATIONS)):
        pcm=design(name,duration,0xB1A57+i*37);encoded,decoded=encode(pcm)
        specs.append((len(bank),len(pcm)));bank+=encoded
        audit.append(dict(name=name,frames=len(pcm),seconds=len(pcm)/RATE,adpcm_bytes=len(encoded),sha256=hashlib.sha256(encoded).hexdigest(),peak=max(map(abs,decoded))))
        audition.extend(decoded);audition.extend([0]*(RATE//4))
    out=ROOT/'src/generated/audio.inc'
    with out.open('w') as f:
        f.write('// Original sounds generated by tools/synthesize_audio.py. IMA ADPCM, 16 kHz.\n')
        f.write('static const struct { uint32_t offset; uint16_t frames; } bc_sound_specs[BC_SOUNDS] = {\n')
        for (offset,frames),name in zip(specs,NAMES): f.write(f' {{{offset}U,{frames}U}}, // {name}\n')
        f.write('};\nstatic const uint8_t bc_sound_data[] = {\n')
        for at in range(0,len(bank),24): f.write(' '+','.join(f'0x{v:02x}' for v in bank[at:at+24])+',\n')
        f.write('};\nstatic const int16_t bc_sine[256] = {\n')
        for at in range(0,256,16): f.write(' '+','.join(str(round(math.sin(i*math.tau/256)*32767)) for i in range(at,at+16))+',\n')
        f.write('};\n')
    (ROOT/'assets/audio-provenance.json').write_text(json.dumps(dict(schema=1,source='tools/synthesize_audio.py',license='MIT',sample_rate=RATE,codec='IMA ADPCM, low nibble first; reset predictor=0/index=0 per voice',bank_bytes=len(bank),sounds=audit),indent=2)+'\n')
    if args.audition:
        with wave.open(str(args.audition),'wb') as wav:
            wav.setnchannels(1);wav.setsampwidth(2);wav.setframerate(RATE)
            wav.writeframes(struct.pack('<'+'h'*len(audition),*audition))
    print(f'{len(NAMES)} original cues: {len(bank):,} ADPCM bytes; 16 kHz PCM16 decoder')
if __name__=='__main__': main()
