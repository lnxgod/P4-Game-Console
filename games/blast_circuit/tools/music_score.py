# SPDX-License-Identifier: MIT
"""Read note data from the two pinned, public-domain Mutopia engravings.

An offline converter only: the cartridge contains compact events, not MIDI or
an interpreter. Source tempo/program/controller messages are deliberately not
executed; each arrangement chooses its own tempo, instruments and accents.
"""
from collections import defaultdict
from pathlib import Path
import struct


def midi_notes(path):
    data = Path(path).read_bytes()
    if len(data) > 65536 or data[:4] != b'MThd':
        raise ValueError('Expected a small standard MIDI score')
    length, kind, count, division = struct.unpack_from('>IHHH', data, 4)
    if length != 6 or kind != 1 or not 1 <= count <= 16 or not 0 < division < 32768:
        raise ValueError('Unsupported score header')
    offset, result = 14, []
    for track in range(count):
        if data[offset:offset + 4] != b'MTrk':
            raise ValueError('Missing track chunk')
        size = struct.unpack_from('>I', data, offset + 4)[0]
        stream = data[offset + 8:offset + 8 + size]
        if len(stream) != size:
            raise ValueError('Truncated track')
        offset += size + 8
        at, tick, running = 0, 0, None
        active = {}

        def take(n):
            nonlocal at
            value = stream[at:at + n]
            if len(value) != n:
                raise ValueError('Truncated event')
            at += n
            return value

        def vlq():
            value = 0
            for _ in range(4):
                byte = take(1)[0]
                value = (value << 7) | (byte & 127)
                if not byte & 128:
                    return value
            raise ValueError('Oversize variable-length integer')

        while at < len(stream):
            tick += vlq()
            if at == len(stream):
                raise ValueError('Missing event')
            status = stream[at]
            if status & 128:
                at += 1
                if status < 240:
                    running = status
            elif running is not None:
                status = running
            else:
                raise ValueError('Missing running status')
            if status == 255:
                take(1)
                take(vlq())
                continue
            if status in (240, 247):
                take(vlq())
                running = None
                continue
            kind, channel = status >> 4, status & 15
            if not 8 <= kind <= 14:
                raise ValueError('Unsupported MIDI status')
            values = take(1 if kind in (12, 13) else 2)
            if any(v >= 128 for v in values):
                raise ValueError('Invalid MIDI data byte')
            if kind == 9 and values[1]:
                key = (channel, values[0])
                if key in active:
                    raise ValueError('Overlapping identical note')
                active[key] = tick
            elif kind in (8, 9):
                start = active.pop((channel, values[0]))
                if tick <= start:
                    raise ValueError('Empty note')
                result.append((track, start, tick, values[0]))
        if active:
            raise ValueError('Unterminated note')
    if offset != len(data):
        raise ValueError('Unexpected trailing data')
    return division, sorted(result)


def arrange_engraving(path, bars, beats):
    division, notes = midi_notes(path)
    phrase_ticks = bars * beats * 8
    events = []
    # Keep both contrapuntal parts at their written pitches. Extra voices are
    # used only for the source's final chord, not automatically guessed chords.
    groups = defaultdict(list)
    for staff, start, end, pitch in notes:
        if start * 8 >= phrase_ticks * division:
            continue
        if (start * 8) % division or (end * 8) % division:
            raise ValueError('Selected passage is not on the 32nd-note grid')
        tick = start * 8 // division
        duration = min(end * 8 // division, phrase_ticks) - tick
        groups[staff, tick].append((pitch, duration))
    for repeat in range(2):
        for (staff, tick), chord in sorted(groups.items()):
            if staff not in (1, 2):
                raise ValueError('Unexpected staff')
            chord.sort(reverse=staff == 1)
            voices = [0, 2, 3] if staff == 1 else [1, 4]
            if len(chord) > len(voices):
                raise ValueError('Score exceeds arrangement polyphony')
            for index, (pitch, duration) in enumerate(chord):
                voice = voices[index]
                velocity = (104 if tick % (beats * 8) == 0 else 92) if voice == 0 else (88 if voice == 1 else 54)
                # Lift the answering voice slightly in the second pass.
                if repeat and voice == 1:
                    velocity += 8
                events.append((repeat * phrase_ticks + tick, duration, pitch, velocity, voice))
    return events, phrase_ticks * 2, phrase_ticks
