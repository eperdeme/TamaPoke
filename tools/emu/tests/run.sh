#!/bin/bash
# Headless tests. They compile the REAL firmware sources against the emulator's
# hardware stubs, so they assert against Pet/Party/Combatant themselves rather
# than restating their rules -- a harness that re-implements a formula only
# proves the transcription.
#
#   bash tools/emu/tests/run.sh          # everything
#   bash tools/emu/tests/run.sh battle   # just the ones matching "battle"
#   TAMA_TEST_JOBS=6 bash tools/emu/tests/run.sh
#
# Two of these exist because nothing else can catch what they catch:
#   flush_test  -- a screen that never calls gfx->flush() leaves the panel
#                  frozen, and headless screenshots CANNOT see it: --shot reads
#                  gfx->buffer() straight out and never consults frameReady.
#                  The training submenu shipped frozen exactly this way.
#   i18n_test   -- STRINGS is positional; a short language row is zero-padded by
#                  the compiler with no diagnostic, silently shifting every
#                  string after the gap in that language only.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
EMU="$(cd "$HERE/.." && pwd)"
ROOT="$(cd "$EMU/../.." && pwd)"
FILTER="${1:-}"
SPRITE_DIR="$ROOT/tools/sdcard/mons"

command -v sdl2-config >/dev/null || { echo "SDL2 not found (brew install sdl2)" >&2; exit 1; }

# sketch.cpp + proto.h come from the normal build; this also proves the emulator
# still compiles before anything is tested against it
OUT="$(mktemp -d)"
OUT_FW="$OUT/firmware.log"
trap 'kill $(jobs -p) 2>/dev/null || true; rm -rf "$OUT"' EXIT

bash "$EMU/build.sh" >/dev/null

# The emulator generates proto.h with every prototype at the top, so it will
# happily compile a sketch that arduino-cli rejects for using a function before
# it is declared. That shipped once. If arduino-cli is installed, the firmware
# build is the one that decides.
fw_pid=""
if command -v arduino-cli >/dev/null; then
  FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"
  # The slowest step, so it runs alongside the suites; its result is checked before the summary.
  arduino-cli compile --fqbn "$FQBN" "$ROOT" >/dev/null 2>"$OUT_FW" &
  fw_pid=$!
fi

# The browser halves of the save backup and the pack upload. They are JavaScript,
# so they cannot run in the C++ harness below -- and they went untested for exactly
# that reason while the firmware side had suites. Gated on node being present, the
# same way the arduino-cli check above is, so a machine without it still runs
# everything else.
if command -v node >/dev/null; then
  echo "=== check_savefile (web/savefile.js)"
  node "$ROOT/tools/check_savefile.mjs" || { echo "    ^ check_savefile FAILED"; exit 1; }
  echo "=== check_packs (web/packs.js)"
  node "$ROOT/tools/check_packs.mjs" || { echo "    ^ check_packs FAILED"; exit 1; }
else
  echo "=== check_savefile, check_packs: SKIPPED (node not installed)"
fi

# arrays, not a string: the sprite dir has to reach the compiler still quoted,
# and passing these through eval silently strips them
CORE=("$ROOT/gbsynth.cpp" "$ROOT/pet.cpp" "$ROOT/i18n.cpp" "$ROOT/party.cpp" "$ROOT/battle.cpp" "$ROOT/link.cpp" "$ROOT/save.cpp" "$ROOT/inventory.cpp" "$ROOT/wild.cpp")
FLAGS=(-std=c++17 -O1 -w -I"$EMU" -I"$ROOT" -DSPRITE_DIR="\"$SPRITE_DIR\"")

# these drive setup()/loop()/render(), so they need the sketch itself
needs_sketch() { case "$1" in touch_test|flush_test|joy_test|anim_test|swipe_test|lan_test|console_test|hit_test|starter_test|release_test|focus_test|explore_test|view_test|transfer_test) return 0;; *) return 1;; esac; }

# and these are standalone: gbsynth.cpp has no Arduino dependency at all, which
# is the point of it -- linking the game core in would only demand stubs for
# symbols the test never calls.
standalone() { case "$1" in synth_test|palette_test|cry_test|crc32_test) return 0;; *) return 1;; esac; }

# sprite_test drives PmdMon straight off the sprite directory, so it needs the
# host's SD stubs but none of the sketch
needs_host() { case "$1" in sprite_test) return 0;; *) return 1;; esac; }

cpu_count=2
if command -v sysctl >/dev/null; then
  cpu_count="$(sysctl -n hw.ncpu 2>/dev/null || echo 2)"
elif command -v getconf >/dev/null; then
  cpu_count="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
fi
case "$cpu_count" in ''|*[!0-9]*) cpu_count=2;; esac
test_jobs="${TAMA_TEST_JOBS:-$cpu_count}"
case "$test_jobs" in ''|*[!0-9]*|0) echo "TAMA_TEST_JOBS must be a positive integer" >&2; exit 1;; esac

# Bash 3 on macOS has no `wait -n`, so throttle on running jobs, not counting the firmware build.
throttle() {
  while (( $(jobs -rp | grep -vx "${fw_pid:-0}" | wc -l) >= test_jobs )); do sleep 0.1; done
}

# Every suite links the same firmware sources, so compile them once rather than per suite.
OBJ="$OUT/obj"
mkdir -p "$OBJ"
CORE_O=()
for f in "${CORE[@]}"; do CORE_O+=("$OBJ/$(basename "$f" .cpp).o"); done
SKETCH_O=("$OBJ/sketch.o" "$OBJ/host_impl.o" "$OBJ/font.o" "$OBJ/clock.o")
HOST_O=("$OBJ/host_impl.o" "$OBJ/font.o")
obj_pids=()
for f in "${CORE[@]}" "$EMU/sketch.cpp" "$EMU/host_impl.cpp" "$EMU/font.cpp" "$EMU/clock.cpp"; do
  throttle
  g++ "${FLAGS[@]}" -c "$f" -o "$OBJ/$(basename "$f" .cpp).o" 2>"$OBJ/$(basename "$f" .cpp).log" &
  obj_pids+=("$!")
done
obj_failed=0
for pid in "${obj_pids[@]}"; do wait "$pid" || obj_failed=1; done
if [ "$obj_failed" -ne 0 ]; then
  echo "=== the shared sources DID NOT COMPILE"
  for log in "$OBJ"/*.log; do
    [ -s "$log" ] && { echo "--- $(basename "$log" .log)"; tail -5 "$log"; }
  done
  exit 1
fi

selected=()
for src in "$HERE"/*_test.cpp; do
  name="$(basename "$src" .cpp)"
  [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]] && continue
  selected+=("$src")
done

run_one() {
  src="$1"
  name="$(basename "$src" .cpp)"
  suite_out="$OUT/$name"
  mkdir -p "$suite_out"
  if [ "$name" = sprite_test ] && [ ! -d "$SPRITE_DIR" ]; then
    echo "=== sprite_test: SKIPPED (generate tools/sdcard/mons with tools/pack_pmd.py)" >"$suite_out/output"
    echo skip >"$suite_out/status"
    return
  fi
  objs=("${CORE_O[@]}")
  standalone "$name" && objs=("$OBJ/gbsynth.o")
  needs_sketch "$name" && objs+=("${SKETCH_O[@]}")
  needs_host "$name" && objs+=("${HOST_O[@]}")
  if ! g++ "${FLAGS[@]}" -o "$suite_out/test" "$src" "${objs[@]}" 2>"$suite_out/compile.log"; then
    { echo "=== $name: DID NOT COMPILE"; tail -5 "$suite_out/compile.log"; } >"$suite_out/output"
    echo fail >"$suite_out/status"
    return
  fi
  # pipefail matters: piping the test through grep would otherwise report
  # grep's exit status and every failure would be counted as a pass
  if (cd "$suite_out" && set -o pipefail && ./test 2>&1 | grep -vE '^(TamaPoke fw|emu:|RTC )') >"$suite_out/test.log"; then
    { echo "=== $name"; cat "$suite_out/test.log"; } >"$suite_out/output"
    echo pass >"$suite_out/status"
  else
    { echo "=== $name"; cat "$suite_out/test.log"; echo "    ^ $name FAILED"; } >"$suite_out/output"
    echo fail >"$suite_out/status"
  fi
}

echo "=== C++ suites (${#selected[@]} total, $test_jobs parallel jobs)"
# Each worker has its own cwd and output files, and emulator NVS lives in process memory.
suite_pids=()
for src in "${selected[@]}"; do
  throttle
  run_one "$src" &
  suite_pids+=("$!")
done
for pid in "${suite_pids[@]}"; do wait "$pid" || true; done

pass=0; fail=0; skip=0
for src in "${selected[@]}"; do
  name="$(basename "$src" .cpp)"
  cat "$OUT/$name/output" 2>/dev/null || echo "=== $name: worker exited without a result"
  result="$(cat "$OUT/$name/status" 2>/dev/null || true)"
  case "$result" in
    pass) pass=$((pass+1));;
    skip) skip=$((skip+1));;
    *) fail=$((fail+1));;
  esac
done

fw_ok=1
if [ -n "$fw_pid" ] && ! wait "$fw_pid"; then
  echo "=== THE FIRMWARE DOES NOT COMPILE (the emulator does; that is not the same thing)"
  grep -i error "$OUT_FW" | head -5
  fw_ok=0
fi
echo
echo "suites passed: $pass, failed: $fail, skipped: $skip"
[ "$fail" -eq 0 ] && [ "$fw_ok" -eq 1 ]
