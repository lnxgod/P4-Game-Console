#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build an original electronic arrangement of a public-domain Bach score.

Source: Sam Franko's 1911 A-minor violin transcription, Sibley Music Library.
The melody follows both strains; ornaments/ties are simplified for a plucked
lead, played one octave lower. New bass, voicings, drums and sound design.
Additional tracks use the public-domain Mutopia score-derived MIDI noted in
assets/music-provenance.json. No performance recordings are used.
"""
import hashlib
import json
import math
from pathlib import Path
import random
from music_score import arrange_engraving

ROOT = Path(__file__).resolve().parents[1]
RATE = 16000
TICK_FRAMES = 800  # Thirty-second notes at quarter = 150 BPM.
# Durations are in thirty-second notes. Each full source bar has 16 ticks.
# Each strain includes its 8-tick pickup and matching final half-bar.
A = [
    'A5:4 C6:2 A5:2',
    'E5:4 A5:2 E5:2 C5:4 E5:2 C5:2',
    'A4:8 E4:2 A4:2 C5:2 A4:2',
    'B4:2 A4:2 B4:2 A4:2 G#4:2 B4:2 D5:2 B4:2',
    'C5:4 A4:4 A5:4 C6:2 A5:2',
    'E5:4 A5:2 E5:2 C5:4 E5:2 C5:2',
    'A4:8 C5:2 B4:2 C5:4',
    'C5:4 C5:4 A5:4 C5:4',
    'C5:4 B4:4 E5:2 D#5:2 E5:4',
    'E5:4 E5:4 C6:4 E5:4',
    'E5:4 D#5:4 B4:2 E5:2 G5:2 E5:2',
    'F#5:2 E5:2 F#5:2 E5:2 D#5:2 F#5:2 A5:2 F#5:2',
    'G5:2 F#5:2 G5:2 F#5:2 E5:2 G5:2 E5:2 D#5:2',
    'E5:2 A5:2 E5:2 D#5:2 E5:2 B5:2 E5:2 D#5:2',
    'E5:2 C6:2 E5:2 D#5:2 E5:2 C6:2 B5:2 A5:2',
    'B5:2 G5:2 F#5:2 E5:2 G5:4 F#5:4',
    'E5:4 R:4',
]
B = [
    'E5:4 G5:2 E5:2',
    'B4:4 E5:2 B4:2 G4:4 B4:2 G4:2',
    'E4:8 Bb3:4 A3:4',
    'D4:4 C#4:2 E4:2 G4:4 F4:2 E4:2',
    'F4:4 D4:4 F5:4 A5:2 F5:2',
    'D5:4 F5:2 D5:2 B4:4 D5:2 B4:2',
    'G4:8 G4:2 C5:2 E5:2 C5:2',
    'D5:2 C5:2 D5:2 C5:2 B4:2 D5:2 F5:2 D5:2',
    'E5:2 D5:2 E5:2 D5:2 C5:2 E5:2 C5:2 B4:2',
    'C5:2 F5:2 C5:2 B4:2 C5:2 G5:2 C5:2 B4:2',
    'C5:2 A5:2 C5:2 B4:2 C5:2 A5:2 G5:2 F5:2',
    'G5:2 E5:2 D5:2 C5:2 E5:4 D5:4',
    'C5:8 E5:2 D5:2 E5:4',
    'E5:4 E5:4 C6:4 E5:4',
    'E5:4 D5:4 D5:2 C5:2 D5:4',
    'D5:4 D5:4 B5:4 D5:4',
    'D5:4 C5:4 A5:4 C6:2 A5:2',
    'G5:4 F5:4 F5:4 A5:1 G5:1 F5:1 E5:1',
    'E5:4 D5:4 D5:4 F5:1 E5:1 D5:1 C5:1',
    'Bb4:2 D5:2 F5:2 D5:2 Bb4:2 A4:2 Bb4:2 A4:2',
    'G#4:4 E4:4 F4:4 E4:4',
    'A4:4 G#4:2 B4:2 D5:4 C5:2 B4:2',
    'C5:4 A4:1 B4:1 C5:1 D5:1 E5:4 C5:2 E5:2',
    'A5:4 E5:1 A5:1 B5:1 C6:1 E6:4 E5:4',
    'A4:4 R:4',
]
# Deliberate accompaniment, not a random chord generator. The source moves
# A minor -> E minor in A; E minor -> D minor -> C major -> A minor in B.
AH = 'Am Am Am E7 Am Am Am Dm B7 Am B7 B7 Em Am Am B7 Em'.split()
BH = 'Em Em A7 Dm Dm G7 C G7 C F F G7 C Am G7 G7 Am Dm Dm Dm E7 E7 Am Am Am'.split()
CHORDS = {
    'Am': ('A2', 'A3 C4 E4'), 'E7': ('E2', 'G#3 D4 E4'),
    'Em': ('E2', 'G3 B3 E4'), 'B7': ('B2', 'A3 B3 D#4'),
    'Dm': ('D2', 'A3 D4 F4'), 'A7': ('A2', 'G3 C#4 E4'),
    'G7': ('G2', 'G3 B3 F4'), 'C': ('C2', 'G3 C4 E4'),
    'F': ('F2', 'A3 C4 F4'),
}

def midi(text):
    if text == 'R':
        return 0
    value = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}[text[0]]
    value += text.count('#') - text.count('b')
    return 12 * (int(text[-1]) + 1) + value


def phrase(bars, harmony, repeat, start):
    assert len(bars) == len(harmony)
    events = []
    time = start
    for bar, (notation, chord) in enumerate(zip(bars, harmony)):
        notes = [(n, int(d)) for n, d in (token.split(':') for token in notation.split())]
        length = sum(d for _, d in notes)
        assert length == (8 if bar in (0, len(bars) - 1) else 16)
        pos = time
        for note, duration in notes:
            if note != 'R':
                # Preserve the melodic contour, including the low B-strain run.
                pitch = midi(note) - 12
                velocity = 108 if (pos - time) % 8 == 0 else 94
                events.append((pos, duration, pitch, velocity, 0))
            pos += duration
        root, chord_notes = CHORDS[chord]
        root = midi(root)
        for off in range(0, length, 4):
            pitch = root if off % 8 == 0 else root + (12 if repeat % 2 else 7)
            events.append((time + off, 3, pitch, 106 if off % 8 == 0 else 76, 1))
        for off in range(4, length, 8):
            for voice, note in enumerate(chord_notes.split(), 2):
                events.append((time + off, 6, midi(note), 34 if repeat == 0 else 44, voice))
        time += length
    return events, time


def drums():
    rng = random.Random(0xBAC1067)
    output = []
    specs = []
    for name, count in [('kick', 3520), ('snare', 2560), ('hat', 640), ('open_hat', 2240)]:
        values = []
        phase = 0.0
        previous = 0.0
        low = 0.0
        for i in range(count):
            t = i / RATE
            noise = rng.uniform(-1, 1)
            if name == 'kick':
                phase += 2 * math.pi * (48 + 95 * math.exp(-t * 48)) / RATE
                value = math.sin(phase) * math.exp(-t * 22) + noise * math.exp(-t * 850) * .22
                gain = 6100
            elif name == 'snare':
                low += (noise - low) * .38
                body = math.sin(2 * math.pi * 181 * t) * math.exp(-t * 34)
                value = .42 * body + .66 * (noise - low) * math.exp(-t * 32)
                gain = 5100
            else:
                value = (noise - previous) * math.exp(-t * (110 if name == 'hat' else 27)) * .45
                gain = 2100 if name == 'hat' else 1450
            previous = noise
            # Short smooth edges, no discontinuity from a nonzero first sample.
            envelope = min(1.0, i / 24, (count - 1 - i) / 80)
            values.append(round(value * envelope * gain))
        specs.append((len(output), count))
        output.extend(values)
    return output, specs


def array(name, values, ctype='int16_t', width=16):
    rows = [', '.join(str(v) for v in values[i:i + width]) for i in range(0, len(values), width)]
    return f'static const {ctype} {name}[{len(values)}]={{\n    ' + ',\n    '.join(rows) + '\n};\n'


def main():
    events = []
    time = 0
    for repeat, (bars, harmony) in enumerate([(A, AH), (A, AH), (B, BH), (B, BH)]):
        new, time = phrase(bars, harmony, repeat, time)
        events.extend(new)
    assert time == 1280
    invention, invention_ticks, invention_phrase = arrange_engraving(
        ROOT / 'assets/music/invention8-mutopia.mid', 34, 3)
    prelude, prelude_ticks, prelude_phrase = arrange_engraving(
        ROOT / 'assets/music/prelude2-mutopia.mid', 32, 4)
    tracks = [
        ('badinerie', events, time, 800, 256, 0),
        ('invention', invention, invention_ticks, 800, invention_phrase, 1),
        ('prelude', prelude, prelude_ticks, 750, prelude_phrase, 2),
    ]
    drums_data, specs = drums()
    text = '// Generated by tools/arrange_music.py. See assets/music-provenance.json.\n'
    text += '// Bach scores: public domain; original arrangements and synth data: MIT.\n'
    text += 'typedef struct { uint16_t tick; uint8_t duration,note,velocity,voice; } bc_note_t;\n'
    for name, sequence, ticks, tick_frames, phrase_ticks, style in tracks:
        sequence.sort(key=lambda e: (e[0], e[4]))
        assert len(set((e[0], e[4]) for e in sequence)) == len(sequence)
        assert all(0 <= e[0] < ticks and 0 < e[1] <= 64 and e[1] * tick_frames < 65536
                   and 24 <= e[2] <= 84 and 0 < e[3] <= 127 and e[4] < 5 for e in sequence)
        # Every monophonic channel must release before its next note.
        ends = [0] * 5
        for tick, duration, note, velocity, voice in sequence:
            assert tick >= ends[voice]
            ends[voice] = tick + duration
        text += f'static const bc_note_t bc_score_{name}[{len(sequence)}]={{\n'
        text += ''.join('    {' + ','.join(map(str, e)) + '},\n' for e in sequence) + '};\n'
    text += 'typedef struct {const bc_note_t *score; uint16_t count,ticks,tick_frames,phrase_ticks; uint8_t style;} bc_track_t;\n'
    text += 'static const bc_track_t bc_tracks[BC_MUSIC_TRACKS]={\n'
    for name, sequence, ticks, tick_frames, phrase_ticks, style in tracks:
        text += f'    {{bc_score_{name},{len(sequence)},{ticks},{tick_frames},{phrase_ticks},{style}}},\n'
    text += '};\n'
    steps = [round(440 * 2 ** ((n - 69) / 12) * 65536 / RATE) for n in range(128)]
    text += array('bc_note_step', steps, 'uint32_t')
    # Band-limited, phase-continuous tables: no square-wave or random-pitch lead.
    for name, harmonics in [('lead', [1, .30, .10]), ('bass', [1, .16, .035]),
                            ('keys', [1, .12, .035]), ('glass_lead', [1, .18, .18])]:
        values = [round(sum(a * math.sin(2 * math.pi * (h + 1) * i / 256)
                            for h, a in enumerate(harmonics)) / sum(harmonics) * 16384)
                  for i in range(256)]
        text += array('bc_wave_' + name, values)
    text += 'static const struct {uint16_t offset,frames;} bc_drum_specs[4]={'
    text += ','.join('{' + f'{o},{n}' + '}' for o, n in specs) + '};\n'
    text += array('bc_drum_data', drums_data)
    target = ROOT / 'src/generated/music.inc'
    target.write_text(text)
    source = ROOT / 'assets/music/badinerie-franko-1911-violin.pdf'
    provenance = {
        'title': 'Blast Circuit / Bach arcade playlist',
        'composer': 'Johann Sebastian Bach (1685–1750)',
        'arrangement_license': 'MIT', 'recordings_used': [],
        'sample_rate_hz': RATE, 'voices': 5, 'percussion_samples': 4,
        'percussion_pcm_bytes': 2 * len(drums_data),
        'rotation': 'Badinerie -> Neon Relay -> Foundry Rush -> Badinerie; continuous across arenas/rounds, 100 ms fades at track boundaries.',
        'duration_seconds': sum(ticks * frames / RATE for _, _, ticks, frames, _, _ in tracks),
        'tracks': [
            {'title': 'Badinerie / Circuit Mix', 'work': 'BWV 1067, Badinerie',
             'source_edition': 'Sam Franko, violin transcription, G. Schirmer, New York, 1911',
             'source_catalog': 'https://hdl.handle.net/1802/26605',
             'source_rights': 'Sibley Music Library explicitly marks this item public domain.',
             'source_files': [str(source.relative_to(ROOT))], 'external_midi_used': False,
             'transcription': 'Approved AABB melody and accompaniment preserved. Ornaments and tied articulations simplified, lead one octave lower.',
             'bpm': 150, 'key': 'A minor with source modulations'},
            {'title': 'Invention No. 8 / Neon Relay', 'work': 'BWV 779',
             'source_edition': 'Bach-Gesellschaft; public-domain engraving by Allen Garvin, Mutopia-2008/06/15-61',
             'source_catalog': 'https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=61',
             'source_rights': 'Mutopia lists Public Domain; LilyPond source includes the typesetter dedication.',
             'source_files': ['assets/music/invention8-mutopia.ly', 'assets/music/invention8-mutopia.mid'],
             'external_midi_used': True,
             'transcription': 'All 34 bars, both contrapuntal parts at written pitch, twice with a stronger answering voice and percussion variation on the second pass. New 3/4 electronic groove.',
             'bpm': 150, 'key': 'F major with source modulations'},
            {'title': 'Prelude in C minor / Foundry Rush', 'work': 'BWV 847, Prelude',
             'source_edition': 'Breitkopf & Hartel, 1866; public-domain engraving by Davide Castellone, Mutopia-2006/07/02-550',
             'source_catalog': 'https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=550',
             'source_rights': 'Mutopia lists Public Domain; LilyPond source includes the typesetter dedication.',
             'source_files': ['assets/music/prelude2-mutopia.ly', 'assets/music/prelude2-mutopia.mid'],
             'external_midi_used': True,
             'transcription': 'Driving passage, source bars 1–32, both parts at written pitch; repeated with percussion breakdown/build. Fixed 160 BPM instead of source tempo changes; later cadenza/Adagio omitted.',
             'bpm': 160, 'key': 'C minor with source modulations'},
        ],
        'generated_include_sha256': hashlib.sha256(target.read_bytes()).hexdigest(),
        'runtime': 'Integer sequencer, five bounded voices, shared original percussion/wavetables, stereo echo; no runtime MIDI, filesystem or external decoder.',
    }
    for item, (_, sequence, ticks, frames, _, _) in zip(provenance['tracks'], tracks):
        item.update(ticks=ticks, tick_frames=frames, duration_seconds=ticks * frames / RATE, events=len(sequence))
        item['source_sha256'] = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in item['source_files']}
    (ROOT / 'assets/music-provenance.json').write_text(json.dumps(provenance, indent=2, ensure_ascii=False) + '\n')
    print(f"Three tracks: {provenance['duration_seconds']:.1f} seconds; "
          f"{sum(len(sequence) for _, sequence, *_ in tracks)} events; shared percussion {2 * len(drums_data)} bytes")

if __name__ == '__main__':
    main()
