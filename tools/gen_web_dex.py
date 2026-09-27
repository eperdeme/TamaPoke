#!/usr/bin/env python3
"""Writes web/dex.json: the species names and the level rule for the installer.

The page labels backups with them ("CHARIZARD Lv.30" rather than "dex 6"), and it
used to re-type MINUTES_PER_LEVEL and MAX_LEVEL to do even that much. Both come
from the game's own sources here, so the page cannot drift from the firmware.

    python3 tools/gen_web_dex.py            # write web/dex.json (build_web.sh does)
    python3 tools/gen_web_dex.py --check    # fail if the committed file is stale
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
from dex_data import DEX  # noqa: E402

OUT = ROOT / 'web' / 'dex.json'


def define(name):
    match = re.search(rf'^#define {name} (\d+)', (ROOT / 'pet.h').read_text(), re.M)
    if not match:
        raise SystemExit(f'gen_web_dex: pet.h has no #define {name}')
    return int(match.group(1))


def build():
    names = [''] * (max(entry[0] for entry in DEX) + 1)
    for num, _slug, display, *_rest in DEX:
        names[num] = display
    data = {
        'schema': 1,
        'minutesPerLevel': define('MINUTES_PER_LEVEL'),
        'maxLevel': define('MAX_LEVEL'),
        'names': names,
    }
    return json.dumps(data, separators=(',', ':')) + '\n'


def main():
    text = build()
    if '--check' in sys.argv[1:]:
        if not OUT.is_file() or OUT.read_text() != text:
            raise SystemExit('web/dex.json is stale; run tools/gen_web_dex.py (build_web.sh does)')
        print('web/dex.json is current')
        return
    OUT.write_text(text)
    print(f'web/dex.json: {len(json.loads(text)["names"]) - 1} species')


if __name__ == '__main__':
    main()
