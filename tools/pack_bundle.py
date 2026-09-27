#!/usr/bin/env python3
"""Empaqueta los sprites de la SD (tools/sdcard/mons/*.bin) en un .pak POR
REGION para que el instalador web los suba de un clic.

ONE FILE PER REGION, not one big one, and that is forced rather than chosen:
Kanto alone is already ~40 MB, so all three would be ~100 MB -- exactly GitHub's
hard per-file limit, which would make the bundle uncommittable. Splitting also
means a player can install Kanto and stop, which is the whole 40 MB most people
want, and add a region later without re-sending what they already have.

Formato TPAK (little-endian):
  char[4]  "TPAK"
  uint16   count
  count x { uint8 nameLen; char name[nameLen]; uint32 size }   (indice)
  ...datos de cada fichero, en el mismo orden...

El instalador (web/index.html) lo descarga, lo parte por el indice y manda cada
fichero a la placa con el protocolo PUT (igual que tools/send_sd.py).
"""
import glob
import json
import os
import struct
import zlib

HERE = os.path.dirname(__file__)
MONS = os.path.join(HERE, 'sdcard', 'mons')
WEB = os.path.join(HERE, '..', 'web')

# DERIVED from dex_data.py, never a second copy of it. The old line here said
# "Must match REGIONS in dex_data.py" and then silently did not: Sinnoh landed
# in the dex and this still stopped at Hoenn, so no sinnoh pack was ever built
# and the installer could not offer one. Same trap as gen_moves.py keeping its
# own DEX_COUNT = 151 while dex.h had grown.
import sys as _sys
_sys.path.insert(0, HERE)
from dex_data import REGIONS as _DEX_REGIONS
REGIONS = [(name.lower(), lo, hi) for name, lo, hi, _starters in _DEX_REGIONS]

GITHUB_LIMIT = 100 * 1024 * 1024
INDEX = os.path.join(WEB, 'paks.json')


def crc_of(blob):
    return format(zlib.crc32(blob) & 0xFFFFFFFF, '08x')


def read_index():
    if not os.path.exists(INDEX):
        return {}
    return json.load(open(INDEX)).get('regions', {})


def write_index(equivalent=None):
    """Describe every pack that exists, for the installer to verify against.

    Separate from write_pak() and callable on its own, because it must be
    possible to regenerate the index from packs that are already committed --
    the sprite workshop is not in the repo, so most checkouts cannot rebuild a
    .pak but can still re-derive its checksum.

    The sizes here are what the buttons are LABELLED with. They used to be typed
    into the HTML by hand and had already drifted: Alola read "~24 MB" for a
    27.5 MB pack. Derive it; never restate it.

    `equivalent` comes from main(); called on its own, a region keeps the list
    it already had only if the file is still exactly the one it was proved for.
    """
    previous = read_index()
    regions = {}
    for index, (name, _lo, _hi) in enumerate(REGIONS):
        path = os.path.join(WEB, f'sprites-{name}.pak')
        if not os.path.exists(path):
            continue
        blob = open(path, 'rb').read()
        count = struct.unpack('<H', blob[4:6])[0] if blob[:4] == b'TPAK' else 0
        crc = crc_of(blob)
        regions[name] = {
            'index': index,
            'bytes': len(blob),
            'crc32': crc,
            'sprites': count,
        }
        if equivalent is not None and name in equivalent:
            same = equivalent[name]
        else:
            old = previous.get(name, {})
            same = old.get('equivalent', []) if old.get('crc32') == crc else []
        if same:
            regions[name]['equivalent'] = same
    with open(INDEX, 'w') as f:
        json.dump({'schema': 1, 'regions': regions}, f, indent=2)
        f.write('\n')
    print(f'{os.path.normpath(INDEX)}: {len(regions)} regions indexed')
    return regions


def dex_of(path):
    base = os.path.basename(path)
    digits = ''.join(c for c in base if c.isdigit())
    return int(digits) if digits else 0


def parse_pak(blob):
    count = struct.unpack('<H', blob[4:6])[0]
    p, index = 6, []
    for _ in range(count):
        n = blob[p]
        name = blob[p + 1:p + 1 + n].decode()
        index.append((name, struct.unpack('<I', blob[p + 1 + n:p + 5 + n])[0]))
        p += 5 + n
    files = {}
    for name, size in index:
        files[name] = blob[p:p + size]
        p += size
    return files


def thumb(tpth, dex):
    """One species' blob out of a TPTH thumbs.bin, or None past its end."""
    count = struct.unpack('<H', tpth[4:6])[0]
    if not 1 <= dex <= count:
        return None
    offs = struct.unpack_from(f'<{count}I', tpth, 6)
    return tpth[offs[dex - 1]:offs[dex] if dex < count else len(tpth)]


def draws_the_same(old, new, lo, hi):
    """Would a card holding `old` show exactly what `new` would, for this region?

    Every region file must be byte-identical. The shared thumbs.bin may differ --
    it grows each time a region is added -- but not in this region's own entries.
    """
    a, b = parse_pak(old), parse_pak(new)
    for name in set(a) | set(b):
        if dex_of(name) or not name.endswith('thumbs.bin'):
            if a.get(name) != b.get(name):
                return False
        elif name not in a or name not in b or any(
                thumb(a[name], d) != thumb(b[name], d) for d in range(lo, hi + 1)):
            return False
    return True


def carried_equivalents(entry, old, new, lo, hi):
    """Older CRCs a card can hold for this region and still draw the current pack.

    This is what stops a new region from telling every player to re-send all the
    others: adding one rewrites thumbs.bin, so every pack's CRC changes while not
    one sprite does.
    """
    if old is None:
        return []
    old_crc = crc_of(old)
    prior = entry.get('equivalent', []) if entry and entry.get('crc32') == old_crc else []
    if old == new:
        return prior
    if draws_the_same(old, new, lo, hi):
        return prior + ([old_crc] if old_crc not in prior else [])
    return []


def write_pak(out, files):
    names = ['mons/' + os.path.basename(f) for f in files]
    blobs = [open(f, 'rb').read() for f in files]
    with open(out, 'wb') as o:
        o.write(b'TPAK')
        o.write(struct.pack('<H', len(files)))
        for name, blob in zip(names, blobs):
            nb = name.encode()
            o.write(struct.pack('<B', len(nb)))
            o.write(nb)
            o.write(struct.pack('<I', len(blob)))
        for blob in blobs:
            o.write(blob)
    return sum(len(b) for b in blobs)


def main():
    files = sorted(glob.glob(os.path.join(MONS, '*.bin')))
    if not files:
        raise SystemExit('no hay sprites en ' + MONS)
    # Files with no dex number in the name -- thumbs.bin -- are SHARED, not part
    # of any region. Splitting by dex range alone dropped thumbs.bin on the
    # floor and the board says so at boot: "sin thumbs.bin (galeria sin
    # miniaturas)".
    #
    # They go in EVERY pack, not just the first. Riding only with Kanto meant
    # anyone who installed Johto or Hoenn WITHOUT Kanto -- which nothing stops
    # them doing -- got no thumbnails at all: bare dex numbers in place of every
    # sprite in the gallery, and no silhouettes. It costs 415 KB against a 27-40
    # MB pack, so there is nothing to weigh up.
    shared = [f for f in files if dex_of(f) == 0]
    previous = read_index()
    equivalent = {}
    made = 0
    for name, lo, hi in REGIONS:
        mine = [f for f in files if lo <= dex_of(f) <= hi]
        if mine:
            mine = sorted(mine + shared)
        if not mine:
            print(f'{name}: no sprites packed yet, skipped')
            continue
        out = os.path.join(WEB, f'sprites-{name}.pak')
        old = open(out, 'rb').read() if os.path.exists(out) else None
        total = write_pak(out, mine)
        equivalent[name] = carried_equivalents(previous.get(name), old,
                                               open(out, 'rb').read(), lo, hi)
        size = os.path.getsize(out)
        flag = '  !! OVER GITHUB LIMIT' if size > GITHUB_LIMIT else ''
        print(f'{os.path.normpath(out)}: {len(mine)} sprites, '
              f'{total / 1048576:.1f} MB datos ({size / 1048576:.1f} MB total){flag}')
        made += 1
    if not made:
        raise SystemExit('nothing packed')
    packed = sum(1 for _ in glob.glob(os.path.join(MONS, '*.bin')))
    print(f'{len(shared)} shared file(s) went into EVERY pack, so any single region works alone')
    write_index(equivalent)


if __name__ == '__main__':
    main()
