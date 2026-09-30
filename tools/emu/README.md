# TamaPoke desktop emulator

Runs the **real firmware** on your computer, in a window you can click.

![emulator](https://img.shields.io/badge/needs-SDL2-1793D1)

```bash
brew install sdl2          # macOS   (Debian: apt install libsdl2-dev)
bash tools/emu/build.sh
tools/emu/tamapoke-emu --scale 2 --fast 60
```

It compiles `TamaPoke.ino`, `pet.cpp`, `i18n.cpp` and `party.cpp` **unmodified**.
Only the hardware layer is replaced, so what you see is what the panel draws —
the same 466×466 RGB565 framebuffer, the same 5×7 font, the same code paths.

## Why

The firmware is otherwise only testable by flashing a board and squinting at it.
This gives you a UI you can iterate on in seconds, and it makes layout mistakes
(text running off the round bezel, bars overflowing) obvious before they reach
hardware. The game logic can also be driven headlessly — see *Headless* below.

**It is not a substitute for the real thing.** Physical touch accuracy, DMA
tearing, board memory pressure, codec output and battery behaviour still need
hardware. The tests below execute the real audio task with a host scheduler;
that is not a measurement of FreeRTOS scheduling or the hardware watchdog.

## Using it

| | |
|---|---|
| **Click** | touch |
| **Drag** | swipe (gestures resolve on release, as on the device) |
| **Hold 3 s** on the pet | the release dialog |
| **Type in the terminal** | the serial console — `STATS`, `IV 31 31 31 31`, `EGG 150 1`, `PARTY`, `LVL 73`, `WIPE`… |
| **Esc** or close the window | quit (state is saved) |

Pixels outside the round bezel are **dimmed**, not hidden: the framebuffer is
square but the panel is a circle, so this shows you exactly what would be
clipped on real hardware.

### Options

| Flag | Meaning |
|---|---|
| `--scale N` | window zoom (default 2) |
| `--fast N` | run the clock N× faster — `--fast 60` turns an in-game minute into a second, so a full 3-day life takes about an hour. The speed-up is **suspended while you are touching the panel**: the firmware times taps and swipes off the same `millis()`, so scaling it during a gesture would shrink the tap window (`dt < 1500`) to `1500/N` real ms and make the screen unclickable |
| `--save FILE` | where to persist NVS (default `tamapoke.nvs` in the cwd) |
| `--wipe` | delete the save first |
| `--sprites DIR` | sprite directory (defaults to `tools/sdcard/mons`) |

### Headless captures

Renders one screen to a PPM and exits — no display needed, so it works over SSH
and in CI:

```bash
tools/emu/tamapoke-emu --shot battle --lvl 73 --iv 31 --dex 149 --out shot.ppm
sips -s format png shot.ppm --out shot.png     # macOS; or use ImageMagick
```

`--shot` accepts `main`, `battle`, `profile`, `medals`, `progress`, `gallery`,
`clock`, `menu`, `party`, `partyfull`, `egg`, `starter`. `--lvl`, `--iv` and
`--dex` set up the pet first.

## How it works

| File | Stands in for |
|---|---|
| `Arduino.h` | `random`, `String`, a `Serial` wired to stdin, and `attachInterrupt` (the SDL layer raises the touch INT by hand — the sketch gates `handleTouch` on it) |
| `clock.cpp` | `millis`, including `--fast` scaling; its own file so the headless tests link the same clock the window runs |
| `Preferences.h` | NVS, backed by a file so your pet survives restarts |
| `Arduino_GFX_Library.h` | the canvas — every primitive the sketch uses, into an RGB565 buffer |
| `TouchDrvCSTXXX.hpp` | the CST9217, fed by the mouse |
| `host_impl.cpp` | SD (reads sprites from disk), RTC, battery, audio |
| `font.cpp` | the classic 5×7 GFX glyphs, so text metrics match exactly |
| `genproto.py` | the prototypes the Arduino build normally generates for you |

Everything except `font.cpp` and `genproto.py` is a stub; the game itself is the
real thing.

`sprites` are read straight from `tools/sdcard/mons/`, so animation, shinies and
Pokédex thumbnails all work if you have generated them (`tools/pack_pmd.py`).
Without them you get the `S_NO_SPRITES` path, exactly as a board with no SD card
would.

## Mandatory Firmware Gates

Install `node`, `python3`, `g++`, `sdl2-config`, `arduino-cli` and esp-emulator
v0.44.0, with the ESP32 core and board libraries. CI pins core 3.3.11, GFX
Library for Arduino 1.6.7, SensorLib 0.4.1 and XPowersLib 0.3.3. Restore sprites
from committed packs once:

```bash
python3 tools/unpack_bundle.py
export TAMA_ESP_EMULATOR=/path/to/esp-emu
TAMA_TEST_JOBS=4 TAMA_TEST_LOG_DIR=/tmp/tamapoke-tests bash tools/emu/tests/run.sh
bash tools/emu/tests/run.sh battle
bash tools/emu/tests/run.sh workflow
TAMA_TEST_BACKEND=host bash tools/emu/tests/run.sh save
node tools/emu/tests/backends.mjs
node tools/emu/tests/mutations.mjs
```

The default is native-first: **29 suites run as ESP32-S3 firmware in
esp-emulator, and 20 host-only suites use the desktop harness**. `backends.json`
is the complete assignment and records why every host-only suite needs that
backend. `backends.mjs` checks it against the actual test files: an unclassified,
stale or duplicate assignment fails, rather than silently choosing a stub.
The emulator is found through `TAMA_ESP_EMULATOR` or `PATH`. A missing native
emulator is a failure, never an automatic host fallback. `TAMA_TEST_BACKEND=host`
is an explicit local debugging override; CI requires native-first mode.

Native suites cover the battle engine/AI and gym simulation, species/learnset/
roster data, evolution and region rules, sleep and clock validation, rewards,
player/creature persistence, legacy upgrades, save export/import, link protocol
and lossy transport, synth/cry samples, palette and CRC calculations. Their
assertions are the same source files compiled for the S3, not new copies of the
rules. Host suites retain synthetic UI/gesture/framebuffer controls, filesystem
sprites, NVS partial-write injection, and resumable task/PCM workflow controls
that the native whole-board setup does not yet provide.

The full runner also requires the real application Arduino compile, both web
suites, native PSRAM/reboot/watchdog probes, native assertion-failure controls
and the original-defect mutations. A substring filter selects suites across both
backends, still requires Arduino and web checks, but omits the full-run probes
and mutations. Missing tools/assets/results, nonzero exits and deadlines are
failures, not skips. Logs are kept when `TAMA_TEST_LOG_DIR` is set. All test saves
are disposable; the runner does not read or overwrite your `tamapoke.nvs` or
backups.

`workflow_test` runs 11 scenarios in four audio modes: enabled, disabled, volume
zero and a full queue of pending effects. The scenarios cover easy and hard gym
wins, wild wins, gym/wild losses and fleeing, capture into the party or box, and
early dismissal of gym/wild victory narration. Each drives the real selection,
battle and result handlers, then runs at least 30 simulated seconds on the main
screen with rendering, touch/menu and serial checks. It requires healthy saving,
then writes real firmware checkpoints to disk and execs a fresh process. Reload
compares creature identity and training, every party/box slot, all badge masks,
both complete dex bitmaps, inventory, player progress and audio settings.

The actual `audio.cpp` task and synth run on a resumable worker thread. Queue
operations and I2S writes yield to a deterministic host scheduler while the
actual sketch runs `loop()`. Enabled victories interrupt an observed active
music note through the result tap handler; muted cases execute zero-valued PCM,
and disabled cases still initialize the task and require blocking waits. The
host starvation detector rejects repeated nonblocking polls without DMA or a
wait. It detects the original busy loop, but is not the ESP32 task watchdog.

Required workflow milestones are battle start, result dismissal, 30-second idle
and fresh-process reward/collection reload. The completion marker must report
all 44 cases. Each case has a 45-second wall-clock deadline, and the runner also
bounds compilation and entire suites. An unexpected firmware restart fails.

`mutations.mjs` builds disposable copies of the real sources. It first proves
the fixed workflow passes, restores either the copied `Pet`/Preferences ownership
defect or the stopped-music active-voices defect, requires the corresponding
saving/starvation failure after battle start, then restores the fix and requires
a pass again. An unrelated failure or timeout is not accepted as detection.
It also restores the premature legacy-party checkpoint write and requires the
combined shorter-party/18-slot-box migration checks to fail, then pass again
after restoration. This protects the migration fix found by the native probe.

The Firmware tests workflow runs the full host and native gates for pushes and
pull requests. The native emulator download is version- and checksum-pinned.
Release publication depends on that reusable workflow finishing successfully;
passing the release metadata checks alone cannot publish a release.

## Native ESP32-S3 Tests

The native suite harness compiles production sources and the assigned test
files into a disposable Arduino image. Each suite runs in a fresh emulator
process with a fresh copy of the compiled flash image, selected through real
UART input. Firmware globals, FreeRTOS clocks and Preferences handles cannot
leak from another suite. Native time remains the real FreeRTOS
clock; ceremony tests wait for their actual deadline and assert the ending
completed. Save completeness/snapshot checks enumerate real NVS keys and read
their typed values. No SDL Arduino or Preferences headers are linked.

Every requested suite must start and complete exactly once, with result zero.
Assertions, panics, unexpected
resets, timeouts or missing milestones fail the gate. Compilation is bounded
at 600 seconds and each native suite at 300 seconds (plus a host kill margin).
Test fixtures use a 64 KB loop-task stack; this does not establish that the
production UI fits its smaller task stack or validate whole-board heap use.

```bash
node tools/emu/tests/native.mjs /path/to/esp-emu --all
node tools/emu/tests/native.mjs /path/to/esp-emu battle_test save_test
node tools/emu/tests/native.mjs /path/to/esp-emu --negative-check
```

The negative check first requires the original CRC suite to pass, corrupts the
production CRC polynomial in a disposable copy, requires the published-value
assertion and native suite result to fail, then restores and requires a pass.
It does not accept an unrelated panic or timeout as assertion coverage.

### Native System Probe

The separate native probe uses Espressif's
[esp-emulator](https://github.com/espressif/esp-emulator) v0.44.0, the same
16 MB flash partition scheme and 8 MB octal PSRAM. It compiles production Pet,
Party, Inventory and save code against the real Arduino/ESP-IDF APIs, without
the SDL Preferences or scheduler doubles:

```bash
node tools/emu/tests/native.mjs /path/to/esp-emu
```

The probe uses a disposable merged flash image. It requires UART application
milestones for PSRAM allocation/content verification, legacy creature/dex/party/
18-slot box migration, checkpoint writes and software reboot. It poisons legacy
keys before reboot so a broken checkpoint reader cannot pass via fallback. A
second emulator process must load the saved flash and preserve rewards. It then
deliberately starves a registered real task watchdog. Version 0.44.0 intercepts
the panic and exits with code 1 rather than rebooting: that negative check must
report the armed milestone, actual task-watchdog failure naming `loopTask`, and
firmware abort, not an unrelated failure or timeout. A third process must reload
the same saved flash successfully. Automatic watchdog reboot/reset-reason
recovery is not emulated by this check. Build and execution logs plus the
disposable flash remain in the printed evidence directory.

This system probe is not whole-board battle/UI/audio integration. The native
battle-engine and synth suites exercise those production algorithms, but the
unmodified board image does boot beyond ROM: debugger
inspection reaches the actual FreeRTOS idle/task-watchdog paths and SD/MMC
initialization. The current whole-board run reports SD host reset error 0x107;
setup completion and audio readiness have not been demonstrated. Sparse UART
output alone is not evidence that S3 or the firmware is incompatible. Keep the
host workflow gate until native board peripherals and complete battle/audio
milestones are verified. Both this native probe and the host workflows are
required by the reusable CI job on which release publication depends.

## Credits

`font.cpp` is the classic 5×7 bitmap font from **Adafruit_GFX**, © 2012 Adafruit
Industries, BSD licence — full notice at the top of that file.
