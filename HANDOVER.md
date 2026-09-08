# TamaPoke — where things stand

Written 2026-08-20, updated through 2026-08-23, so work could resume after a
restart. Read this with `CLAUDE.md`, which is the permanent knowledge and is
**authoritative wherever the two disagree**.

> **§ 1 below is current as of v3.22 (2026-09-08). Sections 0 and 2 to 4c are an
> August snapshot kept for the reasoning in them, and their branch names, version
> numbers and "not merged" notes are long superseded** — `feat/retire-and-release`
> landed, the dex went past Unova to 1025, and the three "failing tests" were
> fixed a dozen releases ago. § 5's pitfalls are the part that has stayed true.
> Do not read anything outside § 1 as a description of the repo today.

---

## 0. Branch `feat/retire-and-release` (v3.6) -- HISTORICAL, landed long ago

Four things, all verified in the emulator; **nothing here has run on a board.**

1. **An early retire no longer banks the creature.** It is gone for good, and
   the next egg is NEUTRAL (`lastEnd = CER_RELEASE`) rather than blessed. It
   used to bank *and* bless exactly as an earned farewell does, so retiring was
   the good ending with a small tax: retire, check the egg, retire again, and
   farm blessed rolls for free. The one-day evolution penalty stays. Retiring
   one that HAS earned its farewell is unchanged -- it is the farewell.
2. **RELEASE on the party/box detail sheet** lets a banked creature go for good.
   It always asks first, and the creature does not fall through into the box.
   The box gained the same sheet: a box tap used to yank the creature into the
   party on one tap, and now opens the sheet with TO PARTY on it.
3. **The 3 s hold is gated on `uiCurrentScreen() == SCR_MAIN`.** It was gated by
   a list of screens to exclude, so it was live on the party screen -- where the
   grid overlaps `inPetZone` and the dialog's YES box sits on party slot 4.
4. **A sprite pack sent over `PUT` now unlocks its region without a reboot.**
   `sdScanRegionArt()` only ever ran at mount, so a region downloaded from the
   web installer stayed greyed out reading NEEDS PACK -- which looks exactly
   like the download having failed. This was reported from the board.

Tests: **34 suites, 34 passing** (`release_test` is new). Every guard was
negative-checked, and two of those checks were themselves found to be vacuous
before they were fixed -- see CLAUDE.md section 3.

Not done: a board has not seen any of it, and the installer has not been rebuilt
(`tools/build_web.sh`), so the published site is still v3.5.

---

## 1. What is live right now

| | |
|---|---|
| Published firmware | **v3.22**, live at https://eperdeme.github.io/TamaPoke/web/ |
| Repo version | **v3.22** in `TamaPoke.ino`, merged to `main` and tagged |
| Dex | `DEX_COUNT` **1025**, `REGION_COUNT` 10, `GYM_REGIONS` 7 |
| Your board | last flashed **v3.20**; v3.21 and v3.22 have not been on hardware |
| Live creature | Venusaur L100, `iv=29/20/19/22 tr=0/16/9`, bond 84, 12 medals |
| Tests | 40 suites + `check_savefile.mjs`, all passing (`sprite_test` skips without sprites) |
| NVS headroom | measured on the board: `used=132 avail=372 total=630` entries |

**Landed since the August snapshot below:** the dex reached 1025; the save is
checkpointed and atomic (issues #3 and #4 closed); the web installer backs up
before flashing and verifies what it captured; and releases are cut from `main`
with `check_release.py` enforcing it.

**Not verified on hardware**, and the first thing to do with a board:

1. The power-key long-press flush — hold PWR and watch for
   `save: flushing (pwr long press)` *before* it dies. If it never appears, the
   AXP2101's long-press IRQ fires too close to its own cut-off and
   `XPOWERS_POWEROFF_6S` in `pwrSetup()` widens the window.
2. The installer's backup-then-flash handoff, the twice-asked port prompt, the
   IndexedDB history and restore-from-history. All of it is untested end to end.
3. `STATS`'s new `board=` line, which is the real `ESP.getEfuseMac()` rather than
   the emulator's fake one.

### Save backups (all in `backups/`, gitignored)

    save-2026-08-21-dragonair-L45.txt          <- the current one, use this
    save-2026-08-20-dratini-maxed.txt          <- Lv 14, an EARLIER state
    save-2026-08-20-pre-3.0.txt
    save-2026-08-19-charizard-MIGRATE.txt      <- the migration block
    save-2026-08-19-board2-marshtomp.txt       <- board 2's original game
    save-2026-08-18.txt

Restore = paste the whole block into the serial console at 115200, **including
the bare `IMPORT` line at the end**, which is the commit. Without it nothing is
written.

---

## 2. The dex expansion — exactly where it stopped

**Goal:** `DEX_COUNT` 386 → 493 (Sinnoh). Chosen because it is the only
generation where every piece exists: 100% sprite coverage, a `pret` disassembly
for verifiable gym rosters, and badge art already reachable.

### Done and working

- **Phase 0, committed.** `--check` over the whole table, `tools/check_sprites.py`,
  and `dexdata_test`. See `CLAUDE.md` § "Adding a generation".
- **Six Gen 1 evolutions linked** (committed): GOLBAT→CROBAT, ONIX→STEELIX,
  CHANSEY→BLISSEY, SEADRA→KINGDRA, SCYTHER→SCIZOR, PORYGON→PORYGON2.
- **The generators are now idempotent** — three consecutive `--emit` runs produce
  byte-identical files. They were append-only before and would have duplicated
  everything on a second run.
- **Data regenerated to 493 — COMMITTED** in `486bacb`, not sitting in the
  working tree as an earlier draft of this file said: `dex_data.py`,
  `dex_types.py`, `dex_stats.py`, `dex_learnsets.py`, `dex.h`, `moves.h`.
  `--check` reports **0 unexpected differences over 1..493**.
- **`--link` picked up Sinnoh's cross-generation evolutions by itself**:
  LICKITUNG→LICKILICKY, RHYDON→RHYPERIOR, TANGELA→TANGROWTH,
  ELECTABUZZ→ELECTIVIRE, MAGMAR→MAGMORTAR and one more. That is the rule working
  as intended — it needs re-running after every expansion.

### The pipeline, in the order it must run

    python3 tools/gen_dex_data.py --emit 493   # dex_data.py + dex_types.py
    python3 tools/fetch_pokeapi.py             # dex_stats.py + dex_learnsets.py
    python3 tools/gen_dex.py                   # dex.h        (needs the stats)
    python3 tools/gen_moves.py                 # moves.h      (needs learnsets)
    python3 tools/gen_dex_data.py --link       # link new cross-gen evolutions
    python3 tools/gen_dex.py                   # again, so the links land
    python3 tools/gen_dex_data.py --check      # must say 0 unexpected

`REGIONS` in `tools/dex_data.py` needs its row added by hand — Sinnoh's is
already in (`('SINNOH', 387, 493, [387, 390, 393])`).

---

## 3. Not started

- ~~**Phase 2 — region gating.**~~ **DONE** (`05a28cb`) — see §7.
- ~~**Sinnoh sprites**~~ **DONE** (`6987c83`) — all 214 files packed, every
  region 100%, `thumbs.bin` regenerated at 493, installer shipping v3.3.
- ~~**Sinnoh gyms and badges**~~ **DONE.** `GYM_REGIONS` is 4, `BADGE_REGIONS`
  is 4, and `verify_rosters.py` checks all three added regions against their own
  disassemblies: **0 of 39 trainers differ.**

  Sinnoh is **Platinum**: Fantina is the THIRD gym, not Diamond/Pearl's fifth.
  The level ramp only runs 14/22/26/32/37/41/44/50 that way, and `roster_test`
  fails a leader 8+ levels below the previous, so the wrong order fails a test.

  `GYM_REGIONS` 3 -> 4 is purely additive for saves: `badgesX[GYM_REGIONS - 1]`
  grows and `getBytes` leaves the shorter stored blob in the front of the bigger
  array, so Johto/Hoenn badges keep their meaning and Sinnoh starts empty.

  **The gym chooser now needs TWO pages** (4 ladders, 3 rows a page), so Sinnoh
  sits on page 2 -- `swipe_test` drives that mode specifically, because the
  dexpick case runs in a different mode and does not speak for it.
- **Trading.** Discussed, not started. `linkMonFrom`/`linkMonTo` already
  serialise a creature both ways, so the exchange is cheap; the hard part is
  atomicity (both sides commit or neither). **Do the radio bring-up first** —
  `linknow.cpp` has never executed on hardware, and building trading on an
  unproven transport means debugging two unknowns at once.

---

## 4. The three failing tests — ALL FIXED (`739a41c`)

Suite is **33/33**. Kept here because two of them are worth knowing about.

**`dexdata_test` — DARKRAI.** Fixed by adding **DARK PULSE** to `dex_moves.py`.
BITE and CRUNCH were the only Dark moves and both are PHYSICAL, so it was never
just Darkrai — every special-attacking Dark type had no special STAB. Darkrai is
SpA 135 / Atk 90 and learns DARK PULSE at level 27. Not added to `NO_ATTACK`,
which stays reserved for Ditto/Unown/cocoons.

**A move's index is its position in `MOVES`, and saves store it RAW.** DARK
PULSE is appended *after* STRUGGLE, not filed under DARK, because inserting
mid-table shifts every later move by one and silently rewrites the moveset of
every saved creature. `dex_moves.py` now carries an `APPEND-ONLY BELOW HERE`
marker. **Any future move goes at the end.**

**`roster_test`.** Asserted `GYM_REGIONS == REGION_COUNT - 1`, which the dex
outgrows the moment a region has data but no roster — the normal state mid
expansion. Now asserts what matters: no ladder for ALL, the table is exactly
`GYM_REGIONS` long, every ladder has a roster and a name.

**`hit_test` — the one worth remembering.** Its "near miss" taps on the egg
region pill were 8 px from the graphic, *inside* the 16 px hit area. They were
direct hits cycling the region twelve times, and it passed only because
`12 % REGION_COUNT(4) == 0` came full circle. Sinnoh made it 5 and it broke. The
dead guard band CLAUDE.md says this test protects **had never been exercised** —
trap 3, a test proving arithmetic rather than firmware. It now taps in the real
band, derives the offset from the rects it already queries instead of copying
`EGGREG_PAD`/`EGGREG_GUARD` into the test, and checks after every tap.

---

## 4a. The overnight runaway (`91aa43c`) — fixed and FLASHED

A player woke to a Dragonair that had run away **after a night of correct
auto-sleep**. It went to bed with all four bars at zero, which armed
`neglectTicks` at 60 *before* the screen went off. The sleeping branch of
`tick()` returns before the neglect block, so the counter was neither counted
nor **cleared** for eight hours, and `canRunawayNow()` read it alone. On waking,
a creature at 100 energy was one tap from gone — and `FAR_BTN` (y176-234) sits
inside `inPetZone` (y95-310) and is checked before the caress, unconfirmed. The
next tick cleared it 60 s later.

`tick()` and `canRunawayNow()` now both ask `Pet::inTotalNeglect()`, so the
counter cannot outlive the state that earned it. `sleep_test` has the
morning-after repro plus a guard that a genuinely empty creature on waking is
still ready to leave. FW_VERSION and the README badge moved to **3.2**.

Flashed and confirmed on the board: `TamaPoke fw v3.3`, pet intact through the
upgrade (NVS is never touched by a USB flash).

    arduino-cli upload -p /dev/cu.usbmodem1101 \
      --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" .

Not done: `FAR_BTN` still overlaps `inPetZone`. The fix removes the cause, not
the delivery mechanism — it can now only fire on a creature genuinely empty
*right now*, which was judged the right line, but the geometry is still the
shape CLAUDE.md §4 warns about.

---

## 4b. Region gating and the installer (`05a28cb`, `6987c83`, `dee84f9`)

**The sprite pack is a real gate.** A region without its `.pak` on the card is
greyed in the chooser reading NEEDS PACK, denies on tap, is skipped by the egg
pill, refused by `setRegion()`, and excluded from the egg pool — `REGION_ALL`
filtering per species so a missing Sinnoh pack cannot put a Sinnoh creature in a
mixed egg. **Locked, never hidden.** Verified live on the board:

    art: KANTO  si
    art: JOHTO  si
    art: HOENN  si
    art: SINNOH NO (falta el pack)

`gRegionArt` defaults to ALL SET and is only narrowed by `sdScanRegionArt()`,
which probes THREE files per region (a half-copied 100 MB pack would pass a
single probe). That default is doing two jobs: no card keeps today's behaviour,
and `pet.cpp` stays free of SD symbols — it links into all 33 test binaries and
none of them build `sdmon.cpp`.

**The chooser is paged**, wraps rather than closing, and `rpickSwipe()` runs
FIRST in `onSwipe()` or the starter screen swallows the gesture. It is in
`swipe_test`, as every paged screen must be.

### The installer, and the trap in it

**The `.pak` files ARE committed and must be.** Release assets send **no CORS
headers**, so a browser `fetch()` of one is blocked — verified with an `Origin`
header: `200`, no `access-control-allow-origin`. `web/README.md` said the exact
opposite ("gitignored", "not committed", release "serves
Access-Control-Allow-Origin"); acting on it untracks them and breaks every
download button for everyone. That happened, in `06db7b9`, and was reverted in
`dee84f9`. The README is rewritten; **`.gitignore`'s comment was the true one.**

Second-order damage worth knowing: removing that bad ignore rule by truncating
the file also deleted the seven lines after it (`tamapoke.nvs`, `tools/_*.svg`,
`backups/`), and a `git add -A` promptly committed the save backups — whose own
comment reads "They were committed by accident once". Edit `.gitignore` by
replacing a block, never by slicing at an index.

`check_installer.py` still passes on every build, guarding both rules that each
destroyed a real save: four parts at their own offsets, and
`new_install_prompt_erase: true`.

## 4c. Unova (`feat/unova`) — DEX_COUNT 649

Two things happened here that had not happened in any earlier expansion.

**1. A region that is not 100% art.** 13 of Unova's 156 have no sprite upstream:

    514 516 520 522 523 538 558 564 565 591 592 616 626

They **keep their dex numbers**. Removing one renumbers every species after it,
and dex numbers are positional in saved data -- `dexReg` bits, `speciesId`,
every party record -- so a deletion corrupts existing saves. What is removed is
their ability to HATCH: `tools/check_sprites.py --emit` writes `noart.h`, and
`pickEggSpecies()`/`rollInRegion()` skip them. `region_test` rolls 2400 eggs and
fails if one appears. **Re-run `--emit` after any expansion and whenever
upstream adds art** -- several of these are base forms, so the list is doing
real work, not covering a theoretical case.

**2. A ladder with nothing to verify it against.** pret's DS disassemblies stop
at Platinum. Unova is therefore written from knowledge, which is precisely how
Johto and Hoenn were first written -- and `verify_rosters.py` later found TEN
errors in those, including two trainers carrying the wrong game's team. So:

- `trainers.h` carries a `*** NOT VERIFIED AGAINST A DISASSEMBLY ***` block
- `verify_rosters.py` prints a NOT VERIFIED section, so "0 trainers differ"
  cannot be read as covering Unova
- `roster_test`'s structural checks still apply, and its monotonic-ramp rule is
  what caught a wrong gym ORDER in Sinnoh earlier

It follows **B2W2**: Black/White's Striaton trio depends on your starter and
Drayden-or-Iris on your version, and one fixed ladder cannot express a choice.
Zebstrika, Throh and Carracosta are on it and have no art -- kept faithful, and
they draw as dex numbers.

**If a Gen 5 decomp ever appears, wire it into `verify_rosters.py` first.**

`check_sprites.py` also had to be fixed: it paged the GitHub API and `break`'d
on any failure, returning a partial set. Rate-limited, it reported all 156 Unova
species as art-less. That was survivable while it only printed a table; it is
not now that the output decides what can hatch. It fails loudly and prefers
authenticated `gh`.

---

## 5. Pitfalls — the ones that have actually cost time

### Build and test

- **A green emulator build does NOT mean the firmware compiles.** `build.sh`
  generates a `proto.h` with every prototype at the top; `arduino-cli` relies on
  auto-prototyping and rejects a function used above its declaration. This shipped
  once with 32 green suites. `tests/run.sh` now runs the real `arduino-cli
  compile` first — do not remove that.
- **Declare new sketch functions above their first use.** Same cause.
- **`const` at namespace scope is internal linkage in C++.** A table the tests
  need must be `extern const`, or the link fails with an undefined symbol.
- **Tests share one NVS store within a process.** A `Pet` built after another one
  slept will LOAD that sleep. Start each case from a known state (`sleep_test`
  has a `fresh()` helper for exactly this).
- **Negative-check every guard.** Break it on purpose and watch the test fail.
  This has caught a bad test roughly as often as a good one — `sprite_test`'s
  first version passed with the bug restored.

### Data and the dex

- **A move's index is its POSITION in `MOVES` (`dex_moves.py`), and saves store
  that index raw.** `gen_moves.py` does `idx = i + 1`; `Pet.moves[]` and
  `PartyMon.moves[]` write it straight to NVS. Inserting a move into its type
  section shifts every move after it by one and silently rewrites the moveset of
  every creature already saved — the live pet and every banked member, on every
  player's device. **New moves go at the end**, after STRUGGLE, under the
  `APPEND-ONLY BELOW HERE` marker. Same family as the box getting its own NVS key
  and badges being stored additively: never reinterpret bytes that already exist.
- **Anything holding a dex number is `int16_t`.** Never `uint8_t`, never the
  literal 151. This trap has fired five times: `evolvesTo`, `TrainerMon::dex`,
  `gen_moves.py`'s own `DEX_COUNT`, the Pokédex page cap, and `PmdMon::load` —
  which drew Ivysaur for Marshtomp and shipped in v2.8.
- **What hides it:** neighbouring code is usually already right, so the screen
  looks half-correct. `SdThumbs::get()` takes an `int16_t`, which is why the
  gallery was perfect while the creature on the main screen was somebody else.
- **A rule enforced in one path but not its twin** is the single most repeated
  mistake here. The TM gate (twice), the swipe paging bug (four times), the
  installer erase, the evolution threshold the card recomputed. Make every caller
  ask ONE function, and **test the caller, not just the rule**.

### The installer (both of these destroyed real saves)

- **The manifest must ship four parts at their own offsets**, never one merged
  image at `0x0` — `merge-bin` pads the gaps and writes 0xFF straight over NVS.
- **`new_install_prompt_erase` must be TRUE.** The name is the exact opposite of
  what it does: `false` means "do not ask, just erase the whole chip".
- `tools/check_installer.py` fails the build on either. `build_web.sh` runs it.

### Hardware

- **A stock board does not appear as a serial port at all.** Hold BOOT, tap
  RESET, release BOOT.
- **The board's RTC is months out** unless set (SETTINGS, or `RTCSET`). The sleep
  window depends on it; an unset clock never auto-sleeps, which fails safe.
- **Chrome holds the serial port** until the page that opened it closes. If
  `send_sd.py` says "Resource busy", close the installer tab.
- **Flashing over USB never touches NVS**; the web installer only leaves it alone
  because of the two fixes above.
- `EXPORT` before anything irreversible. It has been the safety net twice.

### The emulator

- `--save <path>` and `--wipe` keep experiments off your real save.
- `PANIC` / `WDT` at its console fake a crash so the boot report can be seen.
- It is launched here as a background process, so **it has no stdin you can type
  into** — drive it through a FIFO if commands are needed.
- It cannot see: timing, DMA tearing, PSRAM pressure, audio, battery, the radio,
  **touch accuracy** (synthetic taps are exact coordinates) or a missing
  `gfx->flush()`.

---

## 6. Older work still outstanding

- **LAN on two boards.** `linknow.cpp` has never executed. Treat the first
  session as debugging; `linkNowStats()` exists for it.
- **Battle background provenance** in `CREDITS.md` — the last licensing loose end
  before this goes public.
- **Audio by ear.** Nobody has judged the battle loop, the six cues, or whether
  `500 * vol` sounds linear.
- **A 24–48 h soak** on the current build. The last one passed at ~12 h.
