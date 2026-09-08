#!/bin/bash
# Regenera web/firmware/tamapoke.bin (firmware combinado) para el instalador web.
# Uso: bash tools/build_web.sh
set -e
cd "$(dirname "$0")/.."
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"

echo "Compilando..."
arduino-cli compile --fqbn "$FQBN" --export-binaries .

B=build/esp32.esp32.esp32s3
echo "Fusionando binarios..."
# esptool is not on PATH; the Arduino core ships one and that is the version
# that matches the build we just made.
ESPTOOL="$(ls ~/Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool 2>/dev/null | head -1)"
[ -z "$ESPTOOL" ] && ESPTOOL="$(command -v esptool.py || command -v esptool)"
[ -z "$ESPTOOL" ] && { echo "no esptool found"; exit 1; }

# THE FOUR PARTS ARE WHAT THE INSTALLER SHIPS, and that is not a detail.
# A single merged image starts at offset 0 and merge-bin PADS THE GAPS with
# 0xFF, so it writes blank flash straight over the NVS partition at 0x9000 --
# which is the player's save. Every web update destroyed the pet, silently, and
# new_install_prompt_erase:false does not prevent it (that flag only governs the
# extra full-chip erase prompt). Shipping the parts at their own offsets leaves
# 0x9000..0xE000 alone, exactly as arduino-cli's USB upload always has.
cp "$B/TamaPoke.ino.bootloader.bin" web/firmware/bootloader.bin
cp "$B/TamaPoke.ino.partitions.bin" web/firmware/partitions.bin
cp "$B/boot_app0.bin"               web/firmware/boot_app0.bin
cp "$B/TamaPoke.ino.bin"            web/firmware/app.bin

# Still merged for anyone flashing a BLANK board from the command line in one
# shot. It is deliberately NOT in the manifest: it would erase the save.
"$ESPTOOL" --chip esp32s3 merge-bin -o web/firmware/tamapoke.bin \
  0x0     "$B/TamaPoke.ino.bootloader.bin" \
  0x8000  "$B/TamaPoke.ino.partitions.bin" \
  0xe000  "$B/boot_app0.bin" \
  0x10000 "$B/TamaPoke.ino.bin"

echo "OK -> web/firmware/ (4 parts + tamapoke.bin for a blank board)"

# The manifest version is what the installer shows people; keeping it in step
# with FW_VERSION by hand is exactly the sort of thing that silently rots.
FW="$(grep -o '"[0-9.]*"' TamaPoke.ino | head -1 | tr -d '"')"
python3 - "$FW" <<'PYEOF'
import hashlib, json, sys
m = json.load(open('web/manifest.json'))
m['version'] = sys.argv[1]


# Each part carries a hash of its own bytes as a query string.
#
# Pages sits behind a CDN, and manifest.json is small and revalidates while a
# 1.7 MB app.bin does not. Without this a returning browser can pair a NEW
# manifest with a CACHED app.bin -- and bootloader/partitions/app must agree or
# the board boot-loops. The query string is ignored by the server and changes
# whenever the bytes do, so the cache can never silently win.
def part(path, offset):
    digest = hashlib.sha256(open('web/' + path, 'rb').read()).hexdigest()[:16]
    return {'path': f'{path}?v={digest}', 'offset': offset}


# Four parts at their own offsets, never one image at 0 -- see the comment above
# the copies. The gap between 0x8000+partitions and 0xe000 is where NVS lives
# and nothing may be written there.
m['builds'] = [{
    'chipFamily': 'ESP32-S3',
    'parts': [
        part('firmware/bootloader.bin', 0),
        part('firmware/partitions.bin', 0x8000),
        part('firmware/boot_app0.bin',  0xe000),
        part('firmware/app.bin',        0x10000),
    ],
}]
# ALWAYS true, and the name is the exact opposite of what it does. In
# esp-web-tools' no-Improv path -- ours, since this firmware speaks no Improv --
# the Install button reads:
#
#   new_install_prompt_erase ? state = "ASK_ERASE" : _startInstall(true)
#
# So FALSE means "do not ask, just erase", and it calls eraseFlash(), a WHOLE
# CHIP erase that takes NVS with it no matter which parts the manifest lists.
# TRUE shows a screen with an "Erase device" checkbox that starts UNCHECKED, and
# leaving it unchecked installs without erasing -- which, with the four parts
# above, leaves the save untouched.
#
# This was set to False on purpose once, on the belief that the name meant what
# it says. It destroyed two real saves. Do not "fix" it back.
m['new_install_prompt_erase'] = True
json.dump(m, open('web/manifest.json', 'w'), indent=2)
print('manifest version -> ' + sys.argv[1])
PYEOF

# Fails the build if anything the manifest writes would land on the save.
echo "Checking the installer cannot erase a save..."
python3 tools/check_installer.py || { echo "installer would wipe saves -- refusing"; exit 1; }

# The page's save backup, tested. Gated on node so a machine without it can still
# build, but the release check refuses a tag either way -- a backup feature whose
# verification is untested is worse than one that admits it does not verify.
if command -v node >/dev/null; then
  echo "Checking the save backup logic..."
  node tools/check_savefile.mjs >/dev/null || { echo "web/savefile.js FAILED its tests -- refusing"; exit 1; }
else
  echo "node not installed; SKIPPING the save backup tests"
fi

# CACHE KEYS FOR THE JS, computed rather than typed.
#
# index.html carried a hand-maintained ?v= for installer.js, which is a number
# and a promise that drift independently -- and adding a second module made it
# two of them. Pages sits behind a CDN and index.html revalidates while a module
# may not, so a fresh installer.js paired with a STALE savefile.js is a real
# failure, and it would show up as the save backup silently behaving like the old
# version. Same reasoning as the firmware parts above.
#
# Order matters: hash savefile.js, stamp it into installer.js's import, and only
# then hash installer.js -- otherwise index.html points at a digest of a file
# that is about to change.
python3 - <<'PYEOF'
import hashlib, pathlib, re

def key(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()[:16]

save_key = key('web/savefile.js')
installer = pathlib.Path('web/installer.js')
src = installer.read_text()
src, n = re.subn(r"(from '\./savefile\.js)(?:\?v=[0-9a-f]+)?'",
                 lambda m: f"{m.group(1)}?v={save_key}'", src)
if n != 1:
    raise SystemExit(f"expected exactly one savefile.js import in installer.js, found {n}")
installer.write_text(src)

installer_key = key('web/installer.js')
index = pathlib.Path('web/index.html')
html = index.read_text()
html, n = re.subn(r'(src="installer\.js)(?:\?v=[0-9a-f]+)?"',
                  lambda m: f'{m.group(1)}?v={installer_key}"', html)
if n != 1:
    raise SystemExit(f"expected exactly one installer.js script tag, found {n}")
index.write_text(html)
print(f'js cache keys -> savefile {save_key}, installer {installer_key}')
PYEOF

echo "Empaquetando sprites..."
if compgen -G "tools/sdcard/mons/*.bin" >/dev/null; then
    python3 tools/pack_bundle.py
else
    echo "Sin fuentes de sprites; conservando los .pak y regenerando su indice..."
    python3 - <<'PYEOF'
from tools.pack_bundle import write_index
write_index()
PYEOF
fi
