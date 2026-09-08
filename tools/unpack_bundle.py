#!/usr/bin/env python3
"""Restores tools/sdcard/mons/ from the committed web/sprites-*.pak bundles.

    python3 tools/unpack_bundle.py               # every pack that exists
    python3 tools/unpack_bundle.py kanto johto   # just these

The inverse of pack_bundle.py. It exists because the loose per-species sprites
are gitignored build intermediates -- so a fresh checkout has the .pak bundles
but not the directory the emulator reads, which means `tools/make_screens.sh`
silently produces screenshots with NO CREATURE ART. That is worse than leaving
the old ones alone, and it is invisible until somebody looks at the README.

pack_bundle.py's own docstring says "the sprite workshop is not in the repo, so
most checkouts cannot rebuild a .pak". That is true, and it is exactly why the
other direction has to be possible: the .pak files ARE committed, so the bytes
are already here. The alternative was re-fetching ~40 MB per region from
SpriteCollab just to take a screenshot.

Format (little-endian), from pack_bundle.py:

    char[4]  "TPAK"
    uint16   count
    count x { uint8 nameLen; char name[nameLen]; uint32 size }
    ...file data, in the same order...
"""
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
WEB = os.path.join(HERE, '..', 'web')
MONS = os.path.join(HERE, 'sdcard', 'mons')


def unpack(path, into):
    blob = open(path, 'rb').read()
    if blob[:4] != b'TPAK':
        raise SystemExit(f'{path}: not a TPAK bundle')
    count = struct.unpack('<H', blob[4:6])[0]
    at = 6
    index = []
    for _ in range(count):
        name_len = blob[at]
        at += 1
        name = blob[at:at + name_len].decode()
        at += name_len
        size = struct.unpack('<I', blob[at:at + 4])[0]
        at += 4
        index.append((name, size))
    written = skipped = 0
    for name, size in index:
        data = blob[at:at + size]
        at += size
        if len(data) != size:
            raise SystemExit(f'{path}: truncated at {name}')
        # Names are stored as "mons/pNNN.bin"; only the basename is ours to
        # place, and anything trying to escape the directory is refused rather
        # than trusted -- a bundle is a downloaded file like any other.
        base = os.path.basename(name)
        if not base or base != name.removeprefix('mons/'):
            raise SystemExit(f'{path}: refusing suspicious entry {name!r}')
        target = os.path.join(into, base)
        if os.path.exists(target) and os.path.getsize(target) == size:
            skipped += 1
            continue
        with open(target, 'wb') as f:
            f.write(data)
        written += 1
    if at != len(blob):
        print(f'  note: {len(blob) - at} trailing bytes ignored')
    return written, skipped, zlib.crc32(blob) & 0xFFFFFFFF


def main():
    wanted = [a.lower() for a in sys.argv[1:]]
    packs = sorted(
        f for f in os.listdir(WEB)
        if f.startswith('sprites-') and f.endswith('.pak')
        and (not wanted or f[len('sprites-'):-len('.pak')] in wanted)
    )
    if not packs:
        raise SystemExit('no matching web/sprites-*.pak found')
    os.makedirs(MONS, exist_ok=True)
    total = 0
    for pack in packs:
        written, skipped, crc = unpack(os.path.join(WEB, pack), MONS)
        print(f'{pack}: {written} written, {skipped} already present, crc32 {crc:08x}')
        total += written
    have = len([f for f in os.listdir(MONS) if f.endswith('.bin')])
    print(f'{os.path.normpath(MONS)}: {have} sprite files ({total} new this run)')


if __name__ == '__main__':
    main()
