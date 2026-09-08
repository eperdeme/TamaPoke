#!/bin/bash
# Regenerates the screenshots in docs/screens/ that the README shows.
#
#   bash tools/make_screens.sh
#
# They come out of the emulator's headless --shot mode, so they are exactly what
# the firmware draws on the 466x466 panel -- not mockups, and not photographs of
# a screen. Re-run this after changing any screen, or the README slowly starts
# advertising a version of the UI that no longer exists.
set -e
cd "$(dirname "$0")/.."

OUT=docs/screens
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"

# Keep this list in step with the tables in README.md § Screens.
SHOTS="main region starter starterj btlmenu btlmoves gympick gymsj dexpick gallery gallery2 player player2 box egg lanready pick moves win explore wild"

# THE SPRITES HAVE TO BE THERE. tools/sdcard/mons/*.bin is gitignored -- it is a
# build intermediate -- so a fresh checkout renders every creature as a bare dex
# number and these shots come out with NO ART. That is strictly worse than
# leaving the old ones alone, and nothing about it is visible until somebody
# opens the README, which is how the committed set went a dozen releases stale
# without anyone noticing they could not be regenerated.
if [ ! -d tools/sdcard/mons ] || [ -z "$(ls tools/sdcard/mons/*.bin 2>/dev/null)" ]; then
  cat <<'EOF' >&2
No sprites in tools/sdcard/mons, so every creature would draw as a dex number.
Restore them from the committed bundles first -- no network needed:

    python3 tools/unpack_bundle.py

EOF
  exit 1
fi

echo "Building the emulator..."
bash tools/emu/build.sh >/dev/null

# A failed shot is a FAILURE, not a skip. It used to `continue`, which left the
# previous PNG in place -- so renaming a screen quietly published a screenshot of
# something that no longer existed, and the script still said it had finished.
failed=""
for s in $SHOTS; do
  tools/emu/tamapoke-emu --shot "$s" --out "$TMP/$s.ppm" >/dev/null 2>&1 || {
    echo "  $s: FAILED (is it still a --shot name in main_sdl.cpp?)"
    failed="$failed $s"
    continue; }
  if command -v sips >/dev/null; then
    sips -s format png "$TMP/$s.ppm" --out "$OUT/$s.png" >/dev/null
  elif command -v convert >/dev/null; then
    convert "$TMP/$s.ppm" "$OUT/$s.png"
  else
    echo "need sips (macOS) or ImageMagick to convert"; exit 1
  fi
  printf '  %-12s %s KB\n' "$s" "$(( $(wc -c < "$OUT/$s.png") / 1024 ))"
done

if [ -n "$failed" ]; then
  echo
  echo "FAILED:$failed"
  echo "Those screenshots are now STALE, not missing -- the old files are still there."
  exit 1
fi

echo "wrote $(ls "$OUT" | wc -l | tr -d ' ') screenshots to $OUT"
