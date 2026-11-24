#!/usr/bin/env python3
"""Real Game Boy music: pret/pokered audio/music/*.asm -> music.h

    python3 tools/gen_music.py

The .asm files are tracker sequences, not recordings:

    tempo 104
    duty_cycle 3
    note_type 12, 11, 3      ; length unit, volume, envelope
    octave 3
    note F#, 1

so they convert straight into (frequency, duty, volume, envelope, ms) events for
the synth in gbsynth.cpp. 15 KB of text becomes a couple of KB of note data.

FREQUENCY. audio/notes.asm holds twelve 16-bit values; the period is
65536 - value, shifted right by (octave - 1), and the Game Boy register is
2048 - period. That lands C at octave 3 on 261.6 Hz -- middle C -- which is how
the mapping was checked rather than assumed.

TIMING is the engine's own. audio/engine_1.asm gives a note
length * speed * tempo / 256 frames, keeping the remainder for the next note,
and a frame is 1/59.7275 s. An earlier version used a flat 18 ms per length
unit "tuned by ear", which ignored `tempo` and played the gym theme about 2.6x
too slow.

CONTROL FLOW. Channels use sound_call / sound_ret subroutines and counted
sound_loop repeats, so a channel is RUN like the engine runs it, not read top to
bottom. The closing `sound_loop 0, .mainloop` ends the output, and the event
index of .mainloop is emitted as the channel's loop point: the intro before it
plays once, as in the game.

Source: Nintendo / Game Freak / Creatures, via the pokered disassembly. See
CREDITS.md -- the same standing as the trainer sprites.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'music.h')
# The track ids alone, with no note data, so pet.cpp and the sketch can name a
# tune without pulling in the arrays (music.h is static data and would be
# duplicated in every file that includes it).
OUT_IDS = os.path.join(HERE, '..', 'music_ids.h')
BASE = 'https://raw.githubusercontent.com/pret/pokered/master/audio/music/%s.asm'
NOTES_URL = 'https://raw.githubusercontent.com/pret/pokered/master/audio/notes.asm'

# (file, symbol prefix in music.h, the channels' label prefix)
# Two channels: the synth has two pulse voices, and Ch1/Ch2 are melody and
# counter-melody. Ch3 is the wave channel and Ch4 percussion; neither is
# reproduced here yet.
TUNES = [
    ('gymleaderbattle', 'GYM',  'Music_GymLeaderBattle'),
    ('trainerbattle',   'TRN',  'Music_TrainerBattle'),
    ('wildbattle',      'WILD', 'Music_WildBattle'),
    # The victory fanfare. Without it a win played another BATTLE theme, which
    # sounded like the fight had restarted rather than ended.
    ('defeatedgymleader', 'WIN', 'Music_DefeatedGymLeader'),
    # Not battle music: the jukebox. Indices 0-3 above are what audio.cpp plays
    # in a fight. APPEND ONLY: the jukebox's unlocks are saved as a bitmask over
    # these indices, so inserting a tune would unlock the wrong songs.
    ('titlescreen',     'TITLE',  'Music_TitleScreen'),
    ('pallettown',      'PALLET', 'Music_PalletTown'),
    ('lavender',        'LAVENDER', 'Music_Lavender'),
    ('pokecenter',      'CENTER', 'Music_Pokecenter'),
    ('routes1',         'ROUTE1', 'Music_Routes1'),
]

NOTE_NAMES = ['C_', 'C#', 'D_', 'D#', 'E_', 'F_', 'F#', 'G_', 'G#', 'A_', 'A#', 'B_']

FRAME_HZ = 59.7275    # the Game Boy's frame rate, which the sound engine ticks at
MAX_EVENTS = 4000     # per channel; past this the tune is refused, not truncated


def fetch(url):
    r = subprocess.run(['curl', '-fsSL', url], capture_output=True)
    if r.returncode != 0:
        raise SystemExit('fetch failed: ' + url)
    return r.stdout.decode('utf-8', 'replace')


def note_table():
    """The twelve base periods from audio/notes.asm."""
    txt = fetch(NOTES_URL)
    vals = [int(m, 16) for m in re.findall(r'dw \$([0-9A-Fa-f]{4})', txt)][:12]
    if len(vals) != 12:
        raise SystemExit('could not read the note table')
    return [65536 - v for v in vals]      # period at octave 1


def gb_freq(periods, name, octave):
    if name not in NOTE_NAMES:
        return 0
    p = periods[NOTE_NAMES.index(name)] >> max(0, octave - 1)
    if p < 1:
        p = 1
    f = 2048 - p
    return f if 0 <= f < 2048 else 0


def channel_lines(text, symbol):
    """The source lines of one channel, from its label to the next channel's."""
    body = re.split(r'^' + re.escape(symbol) + r'::?\s*$', text, maxsplit=1, flags=re.M)
    if len(body) < 2:
        return []
    m = re.search(r'^\w+_Ch\d+::?\s*$', body[1], flags=re.M)
    body = body[1][:m.start()] if m else body[1]
    return [l.split(';')[0].strip() for l in body.splitlines()]


def first_tempo(text):
    m = re.search(r'^\s*tempo\s+(\d+)', text, flags=re.M)
    return int(m.group(1)) if m else 256


def parse_channel(text, symbol, periods):
    """One channel of one tune -> ([(freq, duty, vol, envDir, envPeriod, ms)], loop).

    Executes the channel the way the engine does: calls, returns and counted
    loops are followed, timing carries its fractional frame like the engine.
    """
    lines = channel_lines(text, symbol)
    labels = {l[:-1]: i for i, l in enumerate(lines) if l.startswith('.') and l.endswith(':')}
    # tempo is global and set once in Ch1; the other channels inherit it
    tempo = first_tempo(text)
    octave, duty, speed, vol, env = 4, 2, 12, 15, 0
    out, seen = [], {}           # seen: label -> event index when first reached
    frac = 0                     # 1/256 frame carried between notes
    frames = 0                   # whole frames elapsed, for drift-free ms
    stack, counters = [], {}
    pc, steps = 0, 0

    def emit(f, d, v, ed, ep, length):
        nonlocal frac, frames
        total = length * speed * tempo + frac
        before = frames
        frames += total >> 8
        frac = total & 0xFF
        ms = round(frames * 1000 / FRAME_HZ) - round(before * 1000 / FRAME_HZ)
        out.append((f, d, v, ed, ep, ms))

    while pc < len(lines):
        steps += 1
        if steps > 200000 or len(out) > MAX_EVENTS:
            raise SystemExit('%s: runaway channel (no sound_loop 0?)' % symbol)
        line = lines[pc]
        pc += 1
        if not line:
            continue
        if line.startswith('.') and line.endswith(':'):
            seen.setdefault(line[:-1], len(out))
            continue
        op = line.split()[0]
        args = [a.strip() for a in line[len(op):].split(',')]
        if op == 'octave':
            octave = int(args[0])
        elif op in ('duty_cycle', 'duty_cycle_pattern'):
            duty = int(args[0]) & 3
        elif op == 'tempo':
            tempo = int(args[0])
        elif op == 'note_type':
            p = [int(x) for x in args]
            speed = p[0]
            if len(p) >= 2: vol = p[1]
            if len(p) >= 3: env = p[2]
        elif op == 'rest':
            emit(0, duty, 0, 0, 0, int(args[0]))
        elif op == 'note':
            f = gb_freq(periods, args[0], octave)
            # env 0..7 fades out, 8..15 fades in; period is the low three bits
            d, per = (-1, env & 7) if env < 8 else (1, env & 7)
            if per == 0:
                d = 0
            emit(f, duty, vol, d, per, int(args[1]))
        elif op == 'sound_call':
            stack.append(pc)
            pc = labels[args[0]]
        elif op == 'sound_ret':
            if not stack:
                return out, 0            # a channel that just ends: loop from the top
            pc = stack.pop()
        elif op == 'sound_loop':
            n, target = int(args[0]), args[1]
            if n == 0:
                if target not in seen:
                    raise SystemExit('%s: loops to %s before reaching it' % (symbol, target))
                return out, seen[target]
            left = counters.get(pc, n) - 1
            if left > 0:
                counters[pc] = left
                pc = labels[target]
            else:
                counters.pop(pc, None)
        # vibrato, pitch_slide, volume, toggle_perfect_pitch: not modelled
    return out, 0


def main():
    periods = note_table()
    sys.stderr.write('middle C check: octave 3 C_ -> %d Hz\n'
                     % (131072 // (2048 - gb_freq(periods, 'C_', 3))))
    lines = ['// GENERATED by tools/gen_music.py from pret/pokered - do not edit',
             '#pragma once', '#include <stdint.h>', '#include "music_ids.h"', '',
             '// Real Game Boy music, as note events for gbsynth.cpp.',
             '// Nintendo / Game Freak / Creatures -- see CREDITS.md.',
             '',
             'struct MusicNote {',
             '  uint16_t freq;   // Game Boy frequency register, 0 = rest',
             '  uint8_t duty;',
             '  uint8_t vol;',
             '  int8_t envDir;',
             '  uint8_t envPeriod;',
             '  uint16_t ms;',
             '};', '']
    index = []
    for fn, pre, sym in TUNES:
        txt = fetch(BASE % fn)
        loops = {}
        for ch in (1, 2):
            ev, loop = parse_channel(txt, '%s_Ch%d' % (sym, ch), periods)
            if not ev:
                raise SystemExit('%s Ch%d: nothing parsed' % (fn, ch))
            loops[ch] = loop
            name = 'MUSIC_%s_CH%d' % (pre, ch)
            lines.append('static const MusicNote %s[%d] = {' % (name, len(ev)))
            for f, d, v, ed, ep, ms in ev:
                lines.append('  { %d, %d, %d, %d, %d, %d },' % (f, d, v, ed, ep, ms))
            lines.append('};')
            lines.append('')
            sys.stderr.write('%-18s Ch%d: %4d events, loop at %4d, %5.1f s\n'
                             % (fn, ch, len(ev), loop, sum(e[5] for e in ev) / 1000))
        index.append((pre, fn, loops[1], loops[2]))
    lines.append('struct MusicTrack {')
    lines.append('  const MusicNote *ch1; uint16_t n1;')
    lines.append('  const MusicNote *ch2; uint16_t n2;')
    lines.append('  uint16_t loop1, loop2;   // where each channel restarts: after the intro')
    lines.append('  const char *name;')
    lines.append('};')
    lines.append('#define MUSIC_TRACKS TUNE_COUNT')
    lines.append('static const MusicTrack MUSIC_TBL[MUSIC_TRACKS] = {')
    for pre, fn, l1, l2 in index:
        lines.append('  { MUSIC_%s_CH1, sizeof(MUSIC_%s_CH1)/sizeof(MusicNote),'
                     ' MUSIC_%s_CH2, sizeof(MUSIC_%s_CH2)/sizeof(MusicNote), %d, %d, "%s" },'
                     % (pre, pre, pre, pre, l1, l2, fn))
    lines.append('};')
    lines.append('')
    open(OUT, 'w').write('\n'.join(lines))
    ids = ['// GENERATED by tools/gen_music.py - do not edit', '#pragma once',
           '#include <stdint.h>', '',
           '// MUSIC_TBL indices. APPEND-ONLY: the jukebox unlocks are saved as a',
           '// bitmask over these, so reordering would hand out the wrong songs.',
           'enum : uint8_t {']
    ids += ['  TUNE_%s = %d,' % (pre, i) for i, (pre, *_r) in enumerate(index)]
    ids += ['  TUNE_COUNT', '};', '']
    open(OUT_IDS, 'w').write('\n'.join(ids))
    sys.stderr.write('wrote %s\n' % os.path.normpath(OUT))


if __name__ == '__main__':
    main()
