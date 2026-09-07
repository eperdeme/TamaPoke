# TamaPoke

Gen-1-Pokémon tamagotchi firmware for the **Waveshare ESP32-S3-Touch-AMOLED-1.75**.
Arduino/C++ firmware + a Python asset pipeline + a browser-based flasher.
Personal, non-commercial fan project. Code MIT; sprites CC BY-NC (PMD SpriteCollab).

## Layout

| Path | What |
|---|---|
| `TamaPoke.ino` | Main sketch (~2.3k LOC): UI, screens, touch, serial console, `FW_VERSION` |
| `pet.cpp/.h` | Game state machine: stats, tick, evolution, eggs, save/load, balance constants |
| `species.h` / `dex.h` | The 151: names, typings, evolution chains, base stats, rarity tiers, favourite berry |
| `types.h` | Type-effectiveness helpers over the generated 18x18 chart in `dex.h` |
| `party.cpp/.h` | The 6 retired pets banked by farewell/release (not runaway) |
| `i18n.cpp/.h` | 6-language string table (ES/EN/FR/DE/IT/PT) |
| `audio.cpp/.h` | ES8311 codec over I2S |
| `rtcbat.cpp/.h` | PCF85063 RTC + AXP2101 battery/PMU/PWR button |
| `sdmon.cpp/.h` | SD sprite streaming + USB `PUT` file transfer |
| `pin_config.h` | Board pinout — from the official Waveshare repo, don't invent values |
| `tools/*.py` | Sprite pipeline (PMD fetch/pack, thumbs, bundle, USB send) |
| `tools/emu/` | Desktop emulator: runs the real firmware in an SDL window |
| `web/` | ESP Web Tools installer page + prebuilt `tamapoke.bin` + `sprites-<region>.pak` (committed: release assets have no CORS) |

## Releasing

`git push origin vX.Y` is the whole release: `.github/workflows/publish-release.yml`
runs `tools/check_release.py` and then publishes. Before tagging:

1. `bash tools/build_web.sh` -- **after the last source edit.** `check_release.py`
   verifies each binary's `?v=` hash against its own bytes, not against the
   source, so a stale `app.bin` passes the check and ships anyway.
2. `FW_VERSION`, the README badge and `web/manifest.json` must all match the tag.
   `build_web.sh` does the manifest; the other two are by hand.
3. **Write `docs/release-notes/vX.Y.md`.** `web/installer.js` reads the release
   body through the GitHub API and puts it straight onto the installer page, so a
   release with no notes greets visitors with "No changelog was provided for this
   release." v3.20 shipped exactly that, because the workflow used
   `--generate-notes` and all that generates is a bare compare link. It uses
   `--notes-file` now and `check_release.py` refuses a tag without one.

   The page renders the body in a `<pre>` with `textContent`, so `##` and `**`
   appear literally. Keep it markdown anyway -- it renders properly on GitHub and
   still reads as plain text there. **Write it for PLAYERS**: what changed, what
   it looked like when it was broken, and whether they need to do anything. No
   file or function names.

Tagging the tip of a feature branch is normal here (v3.18 was), so a release does
not have to wait for a merge to `main`.

## Build & flash

```bash
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"
arduino-cli compile --fqbn "$FQBN" .
arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn "$FQBN" .

bash tools/build_web.sh   # recompiles, writes the 4 parts, bumps the manifest, repacks every region
```

**PSRAM (OPI) is mandatory** — the 466×466×16-bit framebuffer is ~434 KB and lives there.
Wrong partition scheme (no FAT) or PSRAM off = it builds and then fails on hardware.

## Hard rules

**No accents, ñ, or non-ASCII in any firmware string.** The bitmap font has no glyphs
for them. This applies to *all six* languages — French, German, Portuguese and Spanish
strings in `i18n.cpp` are deliberately written unaccented ("Esta", "bano", "Pokedex").
Adding a proper "é" silently renders as garbage on the panel.

**Adding a UI string is a two-file, order-sensitive edit.** Append the `StrId` to the
enum in `i18n.h`, then add the translation at the *same index* in all 6 rows of
`STRINGS[LANG_COUNT][STR_COUNT]` in `i18n.cpp`. The table is positional — a missing
entry in one language shifts every string after it in that language.

**Balance changes must update the README.** `README.md` § "Game manual (the actual
numbers)" documents exact drain rates, spawn odds and thresholds straight from the
code. It is the project's spec, not decoration — changing a constant in `pet.h`/
`pet.cpp` without updating that table makes the docs lie. Bump `FW_VERSION` in
`TamaPoke.ino:27` and the firmware badge at the top of the README in the same commit.

**Comments and commit messages are in English** — as of v1.5. Most of the existing
source is commented in unaccented Spanish (the original author's convention) and is
deliberately *not* being back-translated, so the codebase is mixed for now. Do not
match the surrounding Spanish: any comment you write or edit goes in English, even
in a file that is Spanish everywhere else. Never rewrite a Spanish comment purely to
translate it — only when you are already changing that code for another reason.

The default *UI* language is English (`LANG_DEFAULT LANG_EN`); UI strings live in
`i18n.cpp` and are a separate matter from source comments.

## Traps that have caught us more than once

Every one of these shipped, was found by hand on a board, and had already been
"fixed" somewhere else in the codebase at the time. Read this before touching a
rule, a dex number or a paged screen.

### 1. A rule enforced in one path but not in its twin

This is the single most repeated mistake in the project. The rule is right; a
second caller has its own copy of it, and nobody notices until a player does.

| what | where it was enforced | where it was not | symptom |
|---|---|---|---|
| TM level gate | `relearnFromLevel()` | the STAB guarantee below it | a level 1 Squirtle beat Brock with SURF |
| TM level gate *again* | both of the above | `learnableFor()`, the picker | a level 22 Charmeleon offered FIRE BLAST |
| horizontal swipe = page | one screen at a time | the other three | the same paging bug shipped **four times** |
| save survives an install | the four-part manifest | `new_install_prompt_erase` | fixing one still wiped a real player's pet |
| evolution threshold | `canEvolveNow()` | `renderCardProgress()` | the card would promise an evolution that never came |
| a region needs its pack | `sdBegin()`, at mount | files arriving later over `PUT` | the pack you just downloaded stayed greyed out until a reboot |
| the 3 s hold is for the pet | a list of screens to EXCLUDE | every screen not on that list | holding a PARTY SLOT offered to release the LIVE pet |

**A guard written as a list of EXCLUSIONS is this trap pre-loaded.** The hold
was gated by `!galleryOpen && !cardOpen && !kbOpen && !clockOpen`, so every
screen added afterwards was live by default -- party, box, gym, battle, player,
menu. On the party screen the grid overlaps `inPetZone` AND the dialog's YES box
lands on party slot 4, so a hold plus a tap released the creature you were
raising. It asks `uiCurrentScreen() == SCR_MAIN` now: state what is ALLOWED, not
what is forbidden, or the list rots every time a screen is added.

**The habit:** when you change a rule, `grep` for every caller and make them all
ask ONE function. `moveUnlockLevel()` and `uiButtonDisabled()` exist for exactly
this reason -- they are single answers that three and two callers respectively
used to have their own opinions about.

**And test the CALLER, not just the rule.** Reverting the picker to its own gate
failed NOTHING until `swipe_test` was taught to drive `learnableFor()` itself:
`moves_test` proved the rule was right while the screen ignored it.

### 2. `uint8_t` with a dex of 386

The expansion from 151 to 386 broke five separate things, all the same way, and
they surfaced over months rather than at once:

- `DexEntry::evolvesTo` -- every evolution target above 255 overflowed
- `TrainerMon::dex` -- Hoenn rosters
- `gen_moves.py`'s own `DEX_COUNT = 151` -- emitted a Kanto-sized `LEARN_OFS`
- the Pokedex page cap `if (np > 9) np = 9` -- hid everything past dex 160
- **`PmdMon::load(uint8_t)`** -- 131 species drew Kanto sprites. A MARSHTOMP
  (258) opened `p002.bin` and stood there as an IVYSAUR. Shipped in v2.8.

**The rule: anything holding a dex is `int16_t`, and any loop bound is
`DEX_COUNT`.** Never the literal 151, never `uint8_t`.

**And wearing an INDEX:** a move's index is its position in `MOVES`
(`dex_moves.py`; `gen_moves.py` does `idx = i + 1`), and `Pet::moves[]` /
`PartyMon::moves[]` store that index RAW in NVS. Inserting a move into its type
section shifts every later move by one and silently rewrites the moveset of
every creature already saved -- the live pet and every banked member, on every
player's device. **New moves go at the END**, after STRUGGLE, under the
`APPEND-ONLY BELOW HERE` marker. Same family as the box getting its own NVS key
and badges being stored additively: never reinterpret bytes that already exist.

**The same trap wearing a table instead of a width:** a helper that keeps its
own copy of the REGION list. Four were found at once when Sinnoh landed --
`pack_bundle.py` (whose comment literally read "Must match REGIONS in
dex_data.py" while it did not, so `sprites-sinnoh.pak` could never be built),
`pack_pmd.py`'s `span` dict (so `pack_pmd.py sinnoh` was not a command),
`index.html`'s button ids hardcoded in THREE separate JS loops, and
`renderRegionPick()`/`regionPickTap()` looping `GYM_REGIONS` in all three modes
so **Sinnoh had no row in the Pokedex chooser at all** while the gallery cycled
it happily. Derive the list; never restate it.

**What hides it:** the neighbouring code is often already correct, so the screen
looks half-right and nobody suspects a width. `SdThumbs::get()` takes an
`int16_t`, so the gallery and the silhouettes were perfect while the creature on
the main screen was somebody else entirely.

### 3. A test that proves the transcription rather than the firmware

- `sprite_test` first asserted `m.dex == d` -- which records what was ASKED for.
  With the truncation put back it **still passed**; only comparing the loaded
  PIXELS against the wrap-around species caught it.
- A Python `str.replace` whose anchor did not match silently no-opped, so
  assertions were never inserted and the suite showed green.
- `save_test` has to compare the field table against a store the FIRMWARE wrote,
  not one a restore produced, or it validates itself. It did exactly that until
  it was moved.

**The specific shape, found four times in one session:** an assertion pinned to
a VALUE that happens to hold rather than to the RULE.

- `hit_test` checked the region was unchanged after 12 taps on the egg pill. The
  taps were 8 px out -- INSIDE the 16 px hit area, so every one was a direct
  hit -- and it passed only because `12 % REGION_COUNT(4) == 0` came full
  circle. Sinnoh made it 5 and it broke. The dead guard band that section 4 says
  this test protects had **never once been exercised**.
- `roster_test` asserted `GYM_REGIONS == REGION_COUNT - 1`, true until a region
  had data but no ladder -- which is the normal state mid expansion.
- A new gating check asserted `nextAvailableRegion(0) == 0`, encoding a wrong
  guess about `REGION_ALL` rather than the invariant.
- Worst, a check phrased as a NEGATIVE ("never rests on a locked region") was
  **vacuously true** when nothing was locked, and survived deleting the gate
  entirely. Only the negative-check caught it.

**A DIRECTORY IS NOT COVERAGE.** `check_sprites.py` counted `sprite/NNNN`
existing as "has art" and reported Kalos 100%, while `pack_pmd.py` failed on
0668 PYROAR -- whose directory holds only the form subdirs `0000`/`0001` with no
`AnimData.xml` of its own. The table said complete and the packer disagreed;
the packer was right. It now HEADs the actual sprite per species (curl, in
parallel -- the git tree API comes back truncated at 19 MB and urllib has no
cert store here), which also found four more over-reported species in Alola,
Galar and Paldea. Coverage has to mean "can be drawn", because it decides what
can hatch.

**A tool that reports a partial fetch as fact is the same disease.**
`check_sprites.py` paged the GitHub API and `break`'d on any failure, returning
whatever it had -- so a rate-limited run said an entire generation had no art.
Harmless while it only printed a table; not once that list decides which species
can hatch. It fails loudly now, and sanity-checks the total.

**A test at the END of a long suite is running in whatever state the suite left
behind.** The long-press guard was first checked at the bottom of `touch_test`,
which by then had opened a dozen screens: `cardOpen` was still set, so the hold
was refused for the wrong reason, and the check passed *with the bug put back*.
Twice -- the second time because the panel had DIMMED, and `swallowGesture` eats
a gesture before the gate is consulted at all. It lives in `release_test` now,
from a known state, and asserts the screen it thinks it is on before testing
anything. Prefer a fresh harness to appending onto a long one.

**The habit that catches all of them: break it on purpose and watch the test
fail.** Every guard added since has been negative-checked that way, and it has
caught a bad test roughly as often as it has confirmed a good one. If an
assertion cannot fail, it is not a test. A guard phrased as "X never happens"
needs a companion proving the mechanism actually engaged.

**A test that keeps its own copy of a LAYOUT is the same disease.** `touch_test`
aimed at the menu's STATS row with the literal `104 + 16 + 22`, beside a comment
claiming it was `MENU_ROW_Y(0) + 22`. It never was: it landed ONE PIXEL inside
row 0 and passed for that reason alone. Adding a sixth row moved the boundary
and the same tap started opening the BAG. It asks `uiMenuRowCenterY(i)` now --
the same reason `rpickPageCount()`, `uiButtonHeights()` and `btlCellHit()` are
functions rather than numbers a test may re-type.

**And a state a test leaves behind is the next test's input.** The bag check
added next to it closed the stats card, and the move-picker check three lines
below had always continued from that card being open -- so the suite failed
somewhere other than where it broke. Restore what you borrow, or start fresh.

### 5. sizeof(PartyMon) is load-bearing in four places

Growing the stored record is supported, but only THREE of the four paths that
depend on its size knew that. Appending the care block to `PartyMon` (so a
creature can be swapped in and out without being reset) broke the other one:

| what | how it depends on the size | state before |
|---|---|---|
| `Party::begin()`, the party | migrates a shorter blob by length | correct |
| `Party::begin()`, the box | **no length migration at all** | would have silently emptied every box |
| `save.cpp` `MAX_VAL` | **hardcoded 768**, "the box is the largest, at 18 records" | box dropped from every EXPORT while every other field still restored |
| `SAVE_FIELDS` | must list every NVS key | fine, but the new `bag` key had to be added |

The box one is the dangerous shape: `getBytes` refuses an oversized blob and
leaves the destination alone, so the box reads back EMPTY and the next
`boxSave()` writes that emptiness over the real one. It has the party's
migration now.

`MAX_VAL` is the more instructive one. It was a literal with a comment
explaining the arithmetic, which is a number and a promise that drift
independently -- a backup that is quietly PARTIAL is worse than no backup, and
nothing about the failure was visible except `save_test`. It is
`sizeof(PartyMon) * BOX_SLOTS` now. **Derive it; never restate it** -- the same
rule as § "The same trap wearing a table instead of a width".

`stateVersion` on the record defaults to **0, not 1**, and that is deliberate.
The migration `memcpy`s `oldStride` bytes over a default-constructed record and
leaves the tail alone, so any field defaulting to "I have real data" is a lie
the moment the struct grows again. 0 means "this predates care state", and
`switchTo()` reads the banked level instead.

### 4. What the emulator structurally cannot see

Timing, DMA tearing, PSRAM pressure, audio, battery, the radio -- and two more
that have each cost a real bug:

- **Touch accuracy.** Synthetic taps are exact coordinates, so a target that is
  hard to hit with a finger tests perfectly. The battle grid's bottom row, the
  party BOX button and the egg's region pill were all reported from a board.

  **A small target next to an irreversible action is the dangerous shape.**
  Missing the region pill fell through to `pet.eggTap()`, and three taps hatch
  an egg -- so fumbling at the selector hatched the egg you were re-aiming.
  Where a miss would do something you cannot undo, swallow it: the pill has a
  hit area larger than its graphic AND a dead guard band beyond that, and
  `hit_test` fails if either goes away.
- **Whether the FIRMWARE compiles at all.** `tools/emu/build.sh` generates a
  `proto.h` with every prototype at the top, so the emulator happily builds a
  sketch that `arduino-cli` rejects for using a function before it is declared.
  A green emulator build is not proof the firmware builds. `tests/run.sh` now
  runs the real `arduino-cli compile` first and stops if it fails.
- **A missing `gfx->flush()`.** `--shot` reads `gfx->buffer()` directly and
  never consults `frameReady`, so a frozen panel photographs perfectly. That is
  what `flush_test` is for.

## Testing

**The game logic and the UI can both be tested without a board.** `pet.cpp`,
`party.cpp` and `i18n.cpp` are plain C++ with no hardware dependency beyond
`Arduino.h`/`Preferences.h`, so `tools/emu/` stubs those and compiles the *real*
sources — including `TamaPoke.ino` — into a clickable SDL app:

```bash
brew install sdl2 && bash tools/emu/build.sh
tools/emu/tamapoke-emu --scale 2 --fast 60      # clickable, serial on stdin
tools/emu/tamapoke-emu --shot battle --out s.ppm  # headless screenshot
```

Prefer this for anything about balance, save/load, migrations or layout: it runs
in seconds and can simulate a full 3-day life. Write assertions against the real
classes rather than re-implementing their formulas in the test — a harness that
restates the rules only proves the transcription, not the firmware.

What the emulator can NOT tell you: timing, touch behaviour, DMA tearing, PSRAM
pressure, audio, battery. Those still need the board.

On hardware, verify over the serial console (115200):

- `STATS` full state · `HEALTH` uptime, heap, **PSRAM**, the current screen and
  whether the two battle sprites are resident · `WIPE` factory reset
- The emulator can **fake a crash**: type `PANIC` or `WDT` at it and it parks
  the breadcrumb in a file beside the save, re-execs itself, and comes up
  reporting the crash exactly as the board would. Both are intercepted in
  `main_sdl.cpp` and never reach the firmware -- a serial command that fakes a
  crash has no business existing on hardware.
- **Every boot prints why the last run ended** (`boot: reset=...`). After a
  panic, a watchdog or a brownout it also prints `CRASH: it died on the '<screen>'
  screen with heap=N` -- a breadcrumb kept in RTC memory, which survives a reset
  but not a power cycle. This exists because "it has a mini crash" is not
  actionable: the board never used to say what it was doing. `flush_test`
  asserts the crumb names the screen actually on the panel, since a report that
  points at the wrong screen is worse than none.
- `SPEC <dex>` `LVL <n>` `IV <a> <d> <s> <h>` `HATCH` `SHINY` `EGGS` (20 eggs) `GAL`
- `MISS <n>` set the care mistakes (`desc=` on STATS); each one delays every
  evolution by a level, so `MISS 0` forgives a neglected start
- `TR <atk> <def> <spe>` set the training (this game's EVs); clamped to
  `trMaxFor(iv)`, so it cannot build a creature the player could not raise
- `PARTY` / `PARTY <dex>` / `PARTY CLEAR` inspect, fill and empty the party
- `BATTLE <dex> [lvl]` start a fight (debug entry until gyms exist)
- `EGG <dex> [shiny]` hatch a chosen species: the legendary (3 perfect IVs) and
  shiny (IV floor 20) guarantees fire only inside `hatch()`, so this is the only
  way to reach them — `SHINY` just flips the flag on an already-hatched pet
- `BYE` / `RUN` / `ABANDON` — the three endings · `BEEP` audio · `LS` / `PUT` SD files
- `TIME <epoch>` / `RTCSET <epoch>` — offline-progression and clock paths

To exercise long-horizon logic in minutes, temporarily lower `PET_TICK_MS`,
`MINUTES_PER_LEVEL` and `FAREWELL_AGE_MIN` in `pet.h`.

## Constraints worth remembering

- Sprites are ~40 MB and stream from microSD at runtime; they are never linked into
  the binary. Missing SD = the `S_NO_SPRITES` path, which must stay graceful.
- The pet keeps ageing while powered off via the RTC, catching up to 2 weeks max.
  Any change to tick/aging must survive a large `deltaMinutes` jump without overflow.
- `TamaPoke.ino` is one large sketch by design. Prefer small, surgical diffs over
  refactoring it into modules unless that's the explicit task.

## Git

Feature branches only, never commit straight to `main`.

## Roadmap

The battle system is BUILT -- turn- and move-based, with the gym ladder and the
Elite 4 on two difficulties. The old line here ("resolution by ATK/DEF/SPD,
battle style not yet chosen") described a design that was superseded and is kept
only in the § "Battle system" note explaining why.

What is left is the 24-48 h soak test on `HEALTH`, and **wild encounters**, which
were never built: there is no way to meet a creature outside a gym. That is the
one substantial piece of unimplemented game left, and it now has nowhere obvious
to live -- every gesture from the main screen is taken (see below).

## TODO

Working state, so it survives a closed session. Tick items off as they land.

### Done (branch `feat/battle-foundations`, pushed)

Emulator: touch was dead (`attachInterrupt` was a no-op so the `gTouchIrq` gate
never opened) and `--fast` scaled `millis()`, which the sketch also uses for
gesture timing, shrinking the tap window to `1500/scale` ms. Clock now runs 1x
during a gesture; `millis()` lives in `clock.cpp` so the tests share it.

Firmware: level caps at 100 (which also closes a `uint8_t` overflow the RTC's
two-week catch-up could reach), special-stat accessors, move storage on `Pet`
and `PartyMon` with a length-checked party migration, the moves card page and
on-demand picker, real level gates in the learnsets, level-up learn prompts,
`MoveEntry` ailment fields, the battle engine, the battle screen, and the gym
ladder with badges.

UI fixes: BOND label collided with its bar (label at x=70 size 2 = 12px a
character, bar started at 112 -- three characters, and BOND/LIEN/LACO are four).
The training submenu froze the panel because `renderTrain()` was the one render
path with no `gfx->flush()`. Card pages reordered. Gestures settled: up = the
creature's card, down = the player card, left = the gym ladder, right free.

Minigames: SPEED moved off the ball game onto its own reaction test, so playing
is no longer a stat grind. Three bugs in a row came from the ball game quietly
discarding sessions -- the header tap forfeited (and the ball reaches y=28, well
inside the y<72 quit strip, so reaching for a high ball quit the game), then the
swipe exit forfeited too, and speed trained at `score/5` which integer-divides
to zero below 5 points. All three minigames now bank what was earned on exit.

**Two tests exist because screenshots could not catch these:**
`shotMode` reads `gfx->buffer()` directly and never consults `frameReady`, so a
missing flush is invisible to every capture. One test opens all 13 screens and
asserts each flushes; another checks the 6 x N i18n table for nulls and
non-ASCII. Both live in the scratchpad, NOT the repo -- see below.

### Landed 2026-08-18 (v2.6, merged, installer rebuilt)

The home row's four icons are named constants now (`BTN_FOOD`/`LIGHT`/`BATH`/
`TRAIN`). Dropping the ball icon had shifted every index by one and left
`drawButtons()` reading `i != 2` meaning LIGHT, which had become BATH -- so
asleep, the bath was the only lit icon AND its handler had no sleep guard, so it
really did start a bath. Draw and tap now ask one predicate, `uiButtonDisabled()`,
so a greyed button cannot still be tappable. Icons went 52x52 -> 60x60 and 54 px
apart -> 66, still on the panel arc (`y = 406 - dx^2/729`, outer corner at radius
229.7 of 233). `TR <atk> <def> <spe>` joins `IV` on the console, clamped to
`trMaxFor(iv)`. Three debug prints left the touch path (`BTN n`, `PET`, a
commented `TOUCH x y`) -- they flooded the console while the panel was in use.

**Soak test running from 2026-08-18.** Baseline at up=240s: `heap=254476
min=249568`. Watch the trend, not the value.

### First boot picks a region, then a starter (v2.7)

A new game opens on the region chooser and then shows that region's three
starters; the choice also sets `pet.region`, so the eggs that follow come from
where you started. `starterPick` is `registeredCount() == 0`, so no existing
save ever sees it.

**Do not "tidy" `REGIONS[].starters` to match that screen.** It is also the pool
`rollInRegion()` draws a region's FIRST EGG from, where Kanto's five
deliberately include Pikachu and Eevee. The screen shows the front three;
`starter_test` pins those three per region AND asserts Kanto's pool is still
longer than what is shown, so a reorder or a trim fails a test instead of
silently changing the first screen anyone sees.

The region step is not persisted -- a reset between the two lands back on the
region, which is the harmless direction: nothing has been chosen yet.
`renderRegionPick()` took a mode (`RPICK_FOR_GYMS/DEX/START`) instead of a bool;
at first boot every progress count would read zero, so it shows each region's
first starter as the subtitle and drops the BACK label, there being nowhere to
go back to.

### Next up, roughly in order

~~**Trainer avatars need redrawing.**~~ **done** -- and the licensing call was
made deliberately, so do not quietly reverse it. `avatars.h` is now eight real
Gen 1 overworld sprites generated by `tools/gen_avatars.py` from the
`pret/pokered` disassembly, replacing the four hand-drawn char maps (removed
from `species.h`, along with a comment that had become false).

**This is unlicensed Nintendo art**, unlike the PMD sprites (CC BY-NC) and the
badges (CC BY 3.0). The owner asked for the real sprites after being shown the
trade-off. `CREDITS.md` records it explicitly and recommends shipping
`gen_avatars.py` rather than `avatars.h` if the repo is published -- the script
needs only `curl`, so users fetch the art themselves, exactly the arrangement
already recommended for the PMD sprites.

Notes for anyone regenerating: the sheets are 2bpp greyscale (shade 3 = white =
transparent), six frames stacked, and only frame 0 is used. **Do not pick
`swimmer`** -- the character is in water so the sprite sits low in its cell and
reads as misaligned once it is out of context; `sailor` replaced it. `avatar`
is a `uint8_t` under `"avtr"`, now taken modulo `AVATAR_COUNT` in all three
places that used to mask it with `& 3`, and `pet.cpp` clamps an out-of-range
value so a save from the four-avatar era still loads.

### First bring-up, actually done (2026-08-17)

Two boards flashed with v2.4. What was learned, all of it invisible from here:

- **A stock board does not appear as a serial port at all.** The factory firmware
  does not enable USB CDC, so `ls /dev/cu.*` shows nothing and it looks dead or
  like a bad cable. It enumerates only in DOWNLOAD MODE, as
  `USB JTAG_serial debug unit`: **hold BOOT, tap RESET, release BOOT**. Once our
  firmware is on (built `CDCOnBoot=cdc`) the port stays up while it runs. This is
  the single biggest trap for anyone flashing the first time.
- **Resetting drops the USB device**, because the port IS the firmware. A serial
  monitor opened before the reset holds a stale handle and sees nothing forever;
  reconnect after ~3 s instead of concluding the board hung. It cost a while.
- Boots clean, `heap=255760` and flat over five minutes. PSRAM reads 3.8 MB in
  the factory log, so the OPI part is live.
- **`EXPORT` works on real NVS** (596 bytes on a fresh save) -- the save backup's
  first run outside the emulator.
- A 120 GB card mounts fine: `SD montada: 119850 MB`, `sd=1`.
- **`sin thumbs.bin (galeria sin miniaturas)`** led straight to a real bug: the
  region split silently dropped `thumbs.bin`, because it has no dex number in its
  name and so fell in no region. The Kanto pack was 302 files instead of 303.
  Files with no dex number are shared and now ride with the first pack.

### Audio: a real synth, and a way to HEAR it without a board

`gbsynth.h/.cpp` replaces the single 50% square that made everything sound like
a beeper. Two pulse voices with four duty cycles and volume envelopes, plus a
noise channel, mixed -- which is the actual difference between "beeps" and
"chiptune": a lead at 12.5% duty that decays, over a bass line, with noise for
impacts.

It has NO Arduino dependency on purpose. It fills a buffer and the caller
decides what to do with it: I2S on the board, a WAV file here.

**`tools/emu/tamapoke-emu --wav out.wav --demo two`** is the important part.
Audio was unverifiable for months because the emulator stubs `sfxPlay` to
silence, so the only loop was flash-listen-guess-reflash. Demos: `duty`, `env`,
`noise`, `two`, `tour`.

`synth_test` measures what is measurable -- duty cycles come out at .120/.247/
.495/.750, pitch within 1 Hz of the request, envelopes decaying 22500 -> 4500 ->
0, two voices genuinely mixing, volume 0 truly silent. Whether a tune sounds
GOOD is still ears-only, which is exactly why the WAV export exists.

Still to do: the synth is built and tested but **the firmware still plays the
old tone table** -- wiring `audio.cpp`'s task onto GbSynth is next, then parsing
`pret/pokered`'s `audio/music/*.asm` (same `square_note`/`duty_cycle` vocabulary
as the cries) into note data for the real battle themes.

### Found by hand on the board, not by any test

- **A level 1 Squirtle could beat Brock.** Not the damage formula, which is fine:
  `relearnFromLevel()` was handing newborns the strongest TMs in the table. Its
  two-pass ordering put level-up moves first, but TMs still TOPPED UP the spare
  slots, and a young creature has almost none of its own -- so the set came out
  SURF / OUTRAGE / WATERFALL / BLIZZARD. Worse, the STAB guarantee at the end of
  the function reached past every check to force in the best same-type move,
  which is how SURF survived a first attempt at gating. Both paths are gated by
  `tmLevelFor()` now, roughly power/2.

  The documented ladder curve is unchanged (checked at 40/60/73/100 against the
  table in this file) and matched-level play is sharper than before: Squirtle vs
  Brock is 0% at L12, 65% at L13 -- exactly when WATER GUN unlocks.

- **Touch targets that are fine in the emulator are not fine under a finger.**
  The battle grid's bottom row and the party screen's BOX button were both
  reported as hard to press. Synthetic taps are exact coordinates, so no test
  could have found either. Both now have hit areas larger than their graphics,
  and `hit_test` asserts the battle grid tiles with no dead pixels and that the
  bottom row is not smaller than the top.

### Hardware bring-up order (boards arriving)

Nothing below this line has ever run on a board. Work down it -- each step can
invalidate the ones after it, so a failure early is worth stopping on.

1. **Does an EXISTING save survive the upgrade?** Do this FIRST and on a board
   that already has a pet, because it is the only irreversible one. The dex
   bitmaps went 19 -> 49 bytes and several keys are new; both were designed to
   be additive (`getBytes` leaves a short blob in the front of the array, absent
   keys leave zeroes) and `save_test` covers it, but only real NVS proves it.
   **`EXPORT` before flashing** and keep the block -- that is exactly what it is
   for, and it is the one safety net that exists.
2. **Does it boot and stay up?** Flash is 52% and globals 20%, both comfortable,
   but the PSRAM framebuffer plus two streamed battle sprites is what the
   emulator cannot see. `HEALTH` reports uptime and heap.
3. **The soak test, 24-48 h on `HEALTH`.** Watch the heap trend, not its value.
   This branch added streamed battle sprites, 315 KB of backgrounds, a
   full-screen redraw every frame at 10 fps, an audio task and a second NVS
   blob -- any of which could leak slowly.
4. **AUDIO, which nobody has ever heard.** `BEEP`, then a gym battle for the
   music loop and the six cues. The volume curve (`500 * vol`) is a guess at
   what sounds linear; expect to tune it.
5. **LAN, on TWO boards.** `linknow.cpp` has never executed. Pair from the gym
   chooser's LAN BATTLE button. `linkNowEnd()` prints rx / tx / tx failures /
   packets dropped as another pair's / ring overflows -- read those before
   assuming anything works. Treat the first session as debugging.
6. **Gen 2/3 sprite streaming**, once `pack_pmd.py` has been run for them. Until
   then those species show a dex number, which is the graceful path working.

**0. Soak test -- the one item that can INVALIDATE work rather than add to it.**
24-48 h on hardware with `HEALTH`. Everything built this session is verified in
the emulator only, and the emulator explicitly cannot see timing, DMA tearing,
PSRAM pressure, audio or battery. Since anything last ran on a board this branch
added two streamed battle sprites (~270 KB PSRAM), 315 KB of backgrounds, and a
full-screen background redraw every frame at 10 fps. Needs the board; cannot be
done from here.

**A2. Smaller gaps worth closing.**
- ~~Move relearner~~ **not needed** -- checked: `learnableList()` already lists
  every move learnable at the current level, level-gated ones included, so the
  moves picker recovers a declined move. Verified by declining EMBER/GROWL/LEER
  and finding FLAMETHROWER and WING ATTACK still listed.
- ~~See a banked creature's moves~~ **done** -- tapping a party slot opens its
  sheet: moves with type and power, typing, and the four combat stats.
  `drawMoveRow()` takes a dex now rather than assuming the live pet, so STAB is
  coloured against the creature you are actually looking at.
- ~~A reason to rematch~~ **done**. A gym win now trains the creature that
  fought: 3-5 points on easy, 6-10 on hard, +1 per three leaders deeper into the
  ladder. `Pet::rewardTraining()`.

  Two rules make it work rather than annoy. The stat is random, but **only among
  stats that still have headroom** -- a random grant landing on a maxed stat
  would silently evaporate and read as a bug rather than as luck, and
  `reward_test` fails if the choice is widened. And it never crosses the
  IV-bound ceiling, since a mediocre individual not reaching as far is the point
  of `trMaxFor()`.

  It goes to the LIVE pet only, and only if it was in the squad (`btlPetIn`):
  banked members are frozen at what they were banked with. No cooldown was added
  -- battling already costs the live pet energy, which is the designed
  rate-limit. A fully trained creature is told so instead of seeing nothing
  happen. Balance change, so `README.md` and `FW_VERSION` moved to 2.3 with it.
- ~~Save backup~~ **done**. `EXPORT` prints the whole save as a block of
  `IMPORT <hex>` lines, and pasting that block back is the restore -- there is
  no second format to get wrong and no 2000-character line for a terminal to
  mangle. About 3 KB, 38 lines with the redundant pet and player checkpoints.
  `save_test` prints the figure against `SAVE_TRANSFER_MAX` and fails once it
  passes three quarters of it: `saveExport()` returns 0 rather than truncating,
  so outgrowing the ceiling turns `EXPORT` into `EXPORT FAIL` -- and it would do
  so on the release that grew the dex, not the one that shipped the change.
  **Raise `SAVE_TRANSFER_MAX`; never trim the backup.**

  `save.cpp` is KEY-DRIVEN: `SAVE_FIELDS` lists all 55 keys with their types and
  both directions walk that one table through the ordinary `Preferences` API, so
  the identical code runs on the board and in the emulator. A struct of fields
  would have been a second description of the save that drifts the moment
  somebody adds one. `save_test` compares the table against the keys actually
  present after a save, so a forgotten key fails a test instead of silently
  vanishing from every player's backup -- and that check has to run against a
  store the FIRMWARE wrote, not one a restore produced, or it validates itself
  (it did exactly that until it was moved).

  An import VALIDATES THE WHOLE BLOB before touching NVS: magic, version,
  checksum, and every length. A half-applied restore over a good save would be
  worse than having no backup. It also clears first, so a restore replaces a
  save rather than merging with it. `console_test` drives the real hex out and
  back through `handleSerial()`, including a mistyped digit, an odd-length line
  and a bare commit.

**A. Audio and the win screen -- BACKEND DONE, one UI piece left.**

- ~~Win screen~~ **done**: the badge at 3x with its hard-mode halo, the leader
  named, NEW BADGE! when it is the first time, and the running count.
- ~~Attack sound effects~~ **done**: `SFX_HIT`/`BEAM`/`STATUS`/`SUPER`/`FAINT`/
  `VICTORY`, chosen from the `TurnLog` so the cue can never disagree with what
  happened.
- ~~Battle music~~ **done**: `MUS_BATTLE` loops during a fight, `MUS_VICTORY`
  plays on a win. There is one square-wave voice and one blocking audio task, so
  music is NOT mixed with effects -- the task plays the tune a note at a time and
  hands the voice to any effect that arrives. Effects therefore cut through,
  which is the right priority anyway.
- ~~Volume~~ **done**, backend and UI. `audioSetVolume(0..10)` stored under
  `"vol"` and applied as the square wave's amplitude; the settings row is
  `SND ON | - | VOL n | + | EN >`, with a level bar and both ends clamped. The
  sound switch stays the master; volume is how loud it is when on, and 0 is
  silence without disabling the system.

**A3. ~~Audio is UNHEARD~~ -- HEARD, and it works.** Confirmed on hardware
2026-08-17: `BEEP` produces sound. That closes the oldest unknown in this file.
Still untuned rather than unverified: nobody has judged the music loop, the six
battle cues in context, or whether the `500 * vol` amplitude curve sounds linear
across 0-10. Those need a listen, not a test.

**A3-old. Audio was UNHEARD.** All of the above is verified only by compiling and by
clicking through the emulator, which has no audio at all -- `host_impl.cpp`
stubs `sfxPlay` to nothing. Nobody has heard the music loop, the six cues, or
the volume curve. The amplitude scale in particular (`500 * vol`) is a guess at
what sounds linear. This is part of what the soak test is for.

**B. Storage and the box -- DONE.**
- The box is 18 slots (3 pages of 6) under its OWN NVS key, not a bigger party
  blob. That was deliberate: growing the party blob changes its stride, and the
  length-based migration in `begin()` cannot tell a stride change from a
  slot-count change, so an existing party would have been read back misaligned.
  A separate key is purely additive and cannot corrupt anything -- `box_test`
  checks a pre-box save keeps its whole party and comes up with an empty box.
- `swapPartyBox()` is one call for deposit, withdraw and exchange, since any of
  the two slots may be empty. Reached by tapping a party slot then BOX.
- Room to grow: 6 + 18 records is 720 B against a ~4000 B single-blob limit, so
  the box could reach ~120 before it would need splitting.
- A farewell now falls through party -> box, and only a full party AND a full
  box makes the player choose who to replace. That is what the box is for.

**B3. Bringing a banked creature back -- DONE, frozen.** `Pet::reviveFrom()`
makes a banked creature the live pet as a permanent companion: it does not age,
cannot evolve, and is never offered a farewell or able to run away. Its cost is
that its level never rises again.

`ageMinutes` is simply set to match the banked level rather than adding a second
source of truth, so `level()` needs no special case. Offered ONLY while an egg
is waiting -- otherwise it would silently destroy whatever creature is alive,
and the button says why when it is greyed.

Note for anyone reading the farewell timing: 3 days is when it is first OFFERED
(level 73), not when the creature is finished -- 100 comes at 4d 3h, and
declining re-offers a day later.

**B2. Multi-region, once the Gen 2/3 expansion is untabled.** These four hang
together and should be designed as one thing, not bolted on separately:
- **Region egg switcher** -- choose which generation your eggs come from, so a
  player can run a Kanto game, a Johto game, or mix. Touches `pickEggSpecies()`
  and the rarity tiers, both of which currently assume one flat 1-151 pool.
- **Badges per region** -- `badges`/`badgesHard` are `uint16_t` bitmasks with
  room for 16, so a second region fits, but a third needs widening or an array.
  `badges.h` is Kanto-only; `gen_badges.py` already handles any of the five
  regional SVGs upstream, so the art side is a re-run.
- **Gyms and an Elite 4 per region** -- pure data in the `trainers.h` shape.
- **Swipe left becomes a chooser**: LAN battle or gym battle; gyms then go
  region -> leader. That replaces today's flat list and is what makes multiple
  regions navigable at all.

**C. Multiplayer -- WRITTEN END TO END, RADIO NEVER RUN.**

`link.h`/`link.cpp` hold the whole state machine: hello with a version check,
squad exchange, a guest move, a host result, an end. The transport is a function
pointer, NOT a direct ESP-NOW call, so `link_test` cross-wires two `Link`s in one
process and exercises the entire handshake without a radio. That paid for itself
immediately -- see below.

**Hardened against a real radio (the "bulletproof" pass).** ESP-NOW is best
effort, and the first version assumed delivery everywhere. Three rules now cover
it, all in `link.h`'s header comment: every exchange is stamped with a turn
number so a resend can never be read as a new choice; whatever we last said is
resent until superseded; and `LinkResult` carries ABSOLUTE state, never deltas,
so a guest that misses a turn entirely still lands on the right numbers from the
next one. Every wait has a deadline -- `LINK_LOST` with a message, never a hang.

`lossy_test` is what makes this provable without boards: it drops, duplicates
and silences frames on purpose and asserts twelve turns still complete. It found
two bugs that reading could not:

- **A deadlock.** A side that reached READY stopped answering a peer still
  assembling its squad, and only the side that is behind resends -- so one lost
  SQUAD packet stalled the pair permanently. A finished side now answers a late
  hello with its squad (and NOT another hello, or the two volley forever).
- **A livelock.** Pairing settles into a burst of a fixed length; with one frame
  in three dropped, the same POSITION in the burst died every time and the
  resends never helped -- ten attempts, same packet, always. Squad packets are
  now sent in a ROTATING order so no slot can stay unlucky. Jittering the timer
  does not fix this on its own: the loss is per packet, not per millisecond.
  Real interferers (beacons, microwaves) are periodic, so this is not academic.
  Pairing went from 35 frames to 11 as a side effect.

Also settled: two hosts (or two guests) resolve by id, the higher one hosting,
so the buttons are a preference rather than a trap -- identical ids refuse
rather than guess. A build fingerprint of the table sizes rides in the hello, so
two builds whose `MOVE_TBL` differs refuse instead of narrating different moves.
And NOTHING off the wire is trusted to index a table: `linkMonTo()` clamps dex,
level and every move index, and terminates a name that arrived without a NUL.
`MOVE_TBL[r.hostMove]` on the guest was a straight out-of-bounds read before.

`linknow.cpp` now locks onto the first peer's MAC and unicasts to it, which
stops two pairs of players in one room from joining each other's fights and buys
a real transmit-status callback (a broadcast always reports success). Received
packets are parked in a ring by the WiFi-task callback and drained by
`linkNowPoll()` on the main loop, so protocol state is never touched from
another task and nothing sends from inside the receive callback.

UI: the host LATCHES its own action instead of discarding it when the rival has
not chosen (that made you jab at the move until the timing lined up), both sides
show "waiting for the rival", the guest can now switch by ASKING -- a switch
rides the same message as a move -- and a finished fight returns to the LAN
screen where AGAIN rematches with the squads both sides already hold. Leaving
sends a goodbye so the peer reports at once rather than waiting out a timeout.

Everything around the radio is now built and tested: `linknow.cpp` (ESP-NOW
broadcast), the LAN screen (`renderLan`/`lanOffer`/`lanTap`, reached from a
button on the gym list), and the battle wiring. `btlResolve()` takes the foe's
move from `lan.pendingMove` instead of the AI when `btlLink` is set, ships a
`LinkResult` to the guest, and `btlLinkPoll()` applies it on the guest's side
once a frame. `lan_test` covers that half; `link_test` covers the protocol.

Design points worth not undoing:
- **The squad is rebuilt from `lan.mine`, not from `squadMask`.** What you fight
  with must be exactly what the peer was told you have. Rebuilding from the
  party would silently diverge if anything changed between offering and
  starting -- `lan_test` fails if you switch it back.
- **The peer's team is held as live `Combatant`s** (`btlFoeSquad`), not rebuilt
  from `lan.theirs` each time. A trainer's replacements only ever arrive once; a
  linked opponent can switch out and back, so its creatures must remember how
  battered they are or switching would heal them.
- **An action is one message**, a move slot or a switch with the high bit set,
  because turn matching and resend must have a single path. A move is stored as
  slot+1 so that 0 can keep meaning "nothing chosen yet" -- storing it raw made
  move slot 0 indistinguishable from silence.

Still to do:
- **Run it on two boards.** `linknow.cpp` has never executed. Channel choice,
  delivery, the WiFi/PSRAM interaction and the current draw are all unverified.
  Treat first bring-up as debugging, not as confirmation. `linkNowStats()` was
  added for exactly that first session: rx, tx, tx failures, packets dropped as
  another pair's, and ring overflows, printed on `linkNowEnd()`.
- ~~Team select~~ **done**. HOST/JOIN now opens the same picker the gym ladder
  uses, with `PICK_LAN` (0xFF) as the trainer index -- `squadCap()` already
  returns an uncapped six for anything past the roster, so a LAN battle is
  uncapped by construction rather than by a special case. **Uncapped is the
  decision**: two players who know each other should be able to bring what they
  like, unlike hard mode where the caps are the point.

  `lanOffer()` builds the squad BEFORE bringing the radio up, so what is
  advertised is exactly what was just chosen whether or not the radio comes up.
  `lan_test` drives the picker's FIGHT button and asserts `lan.mine` holds the
  chosen two rather than the whole party -- it fails if the offer is rebuilt
  from anything but `squadMask`.

**The synchronous test transport caught a real re-entrancy bug.** `start()` and
the hello handler both SENT before updating their state, so a reply that arrived
during the call found the sender still `LISTENING` and it answered again --
forever. A real radio is asynchronous and might have hidden this until two
devices with a fast link met. State is now set before sending, and that ordering
matters anywhere `put()` can re-enter.

**D. Licensing, before this repo goes public.** `CREDITS.md` records that the
twelve battle backgrounds in `tools/backs/` have **no established provenance**.
Everything else is accounted for (PMD sprites CC BY-NC, badges CC BY 3.0). Either
confirm their licence or replace them.


0. ~~Fight UI polish~~ **done**. HP plates carry the `HP` label and your own
   side shows numeric HP (`btlSide`). The platform ellipse was dropped on the
   user's call, not forgotten. The layout matches the mainline arrangement:
   foe info top-left / sprite top-right, you bottom-right / bottom-left.
1b. ~~Kanto badge art~~ **done**. `tools/gen_badges.py` needs `rsvg-convert`
   (`brew install librsvg`) and regenerates `badges.h` from the upstream SVG.
   A hard-mode clear draws a golden halo behind its badge.

1. ~~Battle animations~~ **done**, including real PMD playback. `btlPmd[2]`
   streams both combatants (~135 KB PSRAM each, freed when the fight ends) and
   plays `PMD_ATTACK` while lunging, `PMD_HURT` while flinching, `PMD_IDLE`
   otherwise -- each guarded by `has()`, since not every species ships every
   action, falling back to idle and then to the flat thumbnail with no SD.
   The player's slot is NOT the global `pmd`: the active creature may be a
   banked party member rather than the live pet.
2. ~~Trainer name~~ **done**. `Pet::trainerName` is player-wide and outlives
   every ending; tap the title on the player card to set it. The keyboard now
   takes a target (`KB_PET` / `KB_TRAINER`) rather than hardcoding
   `pet.rename()` on commit, so two callers can share it.
3. **Box 6 -> 18** (3 pages of 6). `S_PARTY_FMT` hardcodes "%u/6" in all six
   languages, the party screen needs paging, and **`Party::begin()` must be
   re-keyed off `sizeof(PartyMon)` first** -- it infers the old record size as
   `stored / PARTY_SLOTS`, right when the stride grows and wrong when the slot
   count does, so 180 bytes over 18 slots would infer a 10-byte record.
4. **Peer-to-peer** (see below) -- the biggest, and the only one needing a
   hardware subsystem that has never been brought up.

### Done since the battle plan

Hard mode + battle AI. Both ladders cap your level to the leader's best (without
it a raised team walks everything at 100% and the type chart never matters);
hard also caps team SIZE, uses `HARD_IV` 31 and switches the AI on. Caps are
applied while building the combatants, so the stored creature is never touched.
`aiChooseMove` beats the random chooser **74%** of mirror matches -- `ai_test`
measures that rather than assuming it.

Team select, so which creature you bring is a real decision now that hard mode
caps the size. Player card paging (badges + avatar, then medals). The reaction
test for SPEED, splitting stat training out of the joy game.

### Expanding past Kanto -- PHASE 1 DONE (the dex is 386)

Feasible, and cheaper than it looks. **SpriteCollab already covers Gen 2 and 3**
under the same CC BY-NC licence already in use -- dex 152, 252 and 384 all
return HTTP 200 from the existing `pack_pmd.py` URL. There is no need for
ripped assets from elsewhere; `ZeChrales/PogoAssets` is Niantic art with no
clear licence and would undo the care in CREDITS.md.

**Phase 1 landed: the data.** `DEX_COUNT` is 386, generated end to end.
`tools/gen_dex_data.py` derives dex_data/dex_types from PokeAPI and its
`--check` reproduces the hand-written 151 with zero unexpected differences,
which is what makes it trustworthy for the 235 new ones. Gen 1's entries are
copied through byte for byte and never regenerated -- they are not internally
consistent (stone evolutions vary 30 vs 36 with no rule) and they are already
live in people's Pokedex.

Rarity for the new species is `capture_rate <= 45` among base forms, which
reproduces 23 of the 27 the Gen 1 set picks by hand; the four it misses
(Growlithe, Ponyta, Grimer, Rhyhorn) were chosen for being uncommon in game,
which no data can tell you. Legendary comes straight from PokeAPI.

Three bugs the expansion exposed, all of which had been silently fine at 151:
- `DexEntry::evolvesTo` was `uint8_t`, so every evolution target above 255
  overflowed. Now `uint16_t`.
- `gen_moves.py` had its own `DEX_COUNT = 151`, so it emitted a Kanto-sized
  `LEARN_OFS` while dex.h had grown -- every lookup past 151 would have read
  off the end. It derives the count from `dex_data` now.
- **The Pokedex was capped at 10 pages** (`if (np > 9) np = 9`), which hid
  everything past dex 160. `swipe_test` now walks to the last page and fails if
  any species is unreachable, and the dot row became a page number because 25
  dots do not fit the round panel.

**Phase 2 landed: the egg region.** See § "Choose which region your egg comes
from" in the README for the two anti-farming rules.

**Both multi-region screens open on a REGION CHOOSER**, and that is not
cosmetic. They first shipped opening straight into whichever region was last
set, with a vertical swipe as the only way to move -- which is invisible, so
Johto and Hoenn were built, reachable, and looked absent. The chooser lists each
region with its own progress (badges n/8, or dex n/total), paging back off the
front of a ladder or grid returns to it, and the vertical swipe still works as a
shortcut. `swipe_test` asserts both screens land on it.

**Phase 3 landed: five ladders.** `trainers.h` holds `TRAINERS_KANTO/JOHTO/
HOENN` behind `TRAINER_SETS[GYM_REGIONS]`, and the gym screen changes ladder on
a vertical swipe -- the same gesture the Pokedex uses, and the swipe-left
chooser that was once planned is not needed. `TrainerMon::dex` had to widen to
`uint16_t` (Hoenn runs past 255, the same trap `evolvesTo` fell into).

Badges are stored ADDITIVELY: Kanto keeps `badges`/`badgesHard` under the keys
it has always used, and Johto/Hoenn live in `badgesX`/`badgesHardX` under new
ones. Widening the originals would have meant reinterpreting an existing save;
this cannot. It is the same reasoning that put the box under its own key rather
than growing the party blob. Every read goes through `badgeMask(region, hard)`,
and the running fight keeps `btlRegion` separately from `gymRegion` so that
leaving the gym list mid-battle cannot retarget the badge.

**All three rosters are VERIFIED against the games.** `tools/verify_rosters.py`
diffs Johto and Hoenn against `pret/pokecrystal` and `pret/pokeemerald` -- the
disassemblies, which are the games' own tables and so beat any wiki. It reports
**0 differences across all 39 trainers**. Re-run it after touching a roster.

Sinnoh is **PLATINUM**, and the ladder ORDER differs from Diamond/Pearl:
Fantina is the third gym here, the fifth there. The level ramp is what pins it
-- 14/22/26/32/37/41/44/50 is monotonic only that way, and `roster_test` fails a
leader 8+ levels below the previous, so the D/P order trips a test rather than
shipping. Same shape as Hoenn being Emerald throughout. pokeplatinum stores one
JSON per trainer with rematches in their own files, so unlike Crystal and
Emerald there is no "first party" ambiguity to get wrong.

It found ten, which is why it exists: Lance was missing his Charizard and had
Dragonair where Dragonite belongs, Roxanne was two levels high, Norman and
Winona were the wrong games' teams entirely, and four trainers had their teams
in the wrong ORDER, which matters because the first slot is who leads.

**Hoenn is EMERALD throughout**, and that follows from Juan being the eighth
leader: in Ruby/Sapphire that seat is Wallace's and Steven is champion, while in
Emerald Juan takes the gym and Wallace the title. Mixing them would have given a
ladder that exists in neither game. Emerald's Steven is a post-game rematch at
level 77 and is deliberately absent.

Two findings worth keeping: Johto's leaders really are Kanto-heavy (13 of 49
creatures are Johto natives, against Hoenn's 47 of 57), and Pryce really is
weaker than Jasmine in Gold/Silver, so the ladder dips there on purpose.

**Badge art is done for all three.** `gen_badges.py` now fetches Kanto, Johto
and Hoenn from upstream itself and emits `BADGES_ART[BADGE_REGIONS][8]`; the
player card gained a badge page per region (`PLAYER_PAGES` is `GYM_REGIONS + 1`,
so the page you are on IS the region and no extra control was needed). Sinnoh
and Unova are one line away in `REGIONS` there.

One fix was needed to isolate them: the column finder assumed Kanto's cleanly
separated layout, and Johto's and Hoenn's sheets have neighbours that touch into
a single wide span. It now splits anything much wider than the median column
rather than demanding a layout only Kanto has.

**Phase 2 landed: region gating.** A region is playable only if its sprite pack
is on the card. `sdScanRegionArt()` probes three files per region at mount and
narrows `gRegionArt`; without a pack the chooser row is greyed reading NEEDS
PACK, the pill skips it, `setRegion()` refuses it and the egg pool excludes it,
`REGION_ALL` filtering per species. **Locked, never hidden** -- hiding is how
Johto and Hoenn once looked absent. The mask DEFAULTS to all-set and is only
narrowed by the SD, which gives "no card keeps today's behaviour" for free and
keeps `pet.cpp` free of SD symbols (it links into all 33 test binaries, none of
which build `sdmon.cpp`).

**Phase 4 landed: the sprites.** Kanto through Sinnoh are 100% packed;
`thumbs.bin` regenerates from `DEX_COUNT`.

**Unova landed (DEX_COUNT 649), and brought two firsts.**

1. **A region that is NOT 100% art.** 13 of Unova's 156 have no sprite upstream.
   They KEEP THEIR DEX NUMBERS -- removing one renumbers every species after it,
   and dex numbers are positional in saved data (`dexReg` bits, `speciesId`,
   every party record) -- but they are barred from the EGG POOL, because
   hatching one gives a creature that can only ever draw as a number.
   `tools/check_sprites.py --emit` writes `noart.h`; `region_test` rolls 2400
   eggs and fails if one appears. **Re-run `--emit` after any expansion, and
   again whenever upstream adds art.**
2. **A ladder with no disassembly to check it against.** pret's DS work stops at
   Platinum, so Unova is written from knowledge -- which is exactly how Johto and
   Hoenn were first written, and `verify_rosters.py` later found TEN errors in
   them. `trainers.h` marks it, and the script prints a NOT VERIFIED section so
   that "0 trainers differ" can never be read as covering it. It follows
   **B2W2**, since Black/White's Striaton trio depends on your starter and
   Drayden-or-Iris on your version, and a fixed ladder cannot express a choice.

   **Four roster slots are now DELIBERATE SUBSTITUTIONS**, reversing the line
   above about keeping art-less mons and letting them draw as numbers. Three of
   the four were the LEAD -- the first creature you see when a fight opens -- so
   a bare dex number was the opening image of Elesa's, Marlon's, Marshal's and
   Malva's battles. `roster_test` now FAILS if any team contains a species from
   `noart.h` and prints which trainer and slot, so a future generation cannot
   reintroduce it quietly; Alola, Galar and Paldea all carry art-less species.
   Every stand-in keeps the leader's specialty type and sits near the original's
   base-stat total. The cost: Unova no longer matches B2W2 exactly, and
   `verify_rosters.py` cannot catch that, there being no Gen 5 decomp to diff
   against. `trainers.h` records which four and why.

What changed for 386 species:

- `dex.h`, `moves.h` and the learnsets **regenerate** -- `gen_dex.py` already
  loops `range(1, DEX_COUNT + 1)` and `fetch_pokeapi.py` fetches by number.
- `dexReg[19]`/`dexShinyReg[19]` -> `[49]`. This migrates safely on its own: a
  shorter stored blob reads into the front of the bigger array, so bits 1-151
  keep their meaning.
- **Eight places use the literal `151` instead of `DEX_COUNT`** -- `pet.cpp`
  lines ~243, 256, 283, 295, 572, 592 and `pet.h` `isRegistered`/
  `isShinyRegistered`. These are the ones that will bite.
- `"POKEDEX %u/151"` is hardcoded in all six languages.
- Sprites on the SD go 40 MB -> ~135 MB. Fine on a card.
- ~~The blocker is the web installer~~ **solved, and not by making the load
  optional.** The bundle is ONE FILE PER REGION (`web/sprites-<region>.pak`,
  40/27/38/33 MB), so nobody flashes 140 MB in a browser -- most people take
  Kanto and stop. It is also forced rather than chosen: GitHub's hard per-file
  limit is 100 MB and a single bundle would be uncommittable.
- **The `.pak` files ARE committed, and must be.** They have to be served
  same-origin from Pages, because **GitHub release assets send no CORS headers
  at all** -- a browser `fetch()` of one is blocked. Verified with an `Origin`
  header: `200`, and no `access-control-allow-origin`. `web/README.md` claimed
  the exact opposite for a long time and acting on it untracks them and breaks
  every download button. The code comment in `.gitignore` was the true one.
- `dex_moves.py` is 77 moves hand-picked so every *Kanto* typing has a STAB
  option; Hoenn adds species that would need coverage added.
- Johto/Hoenn gyms are pure data, in the shape `trainers.h` already uses.

### The three drain paths, and why only one had no floor

Worth knowing before touching balance, because the asymmetry cost a player a
creature:

| path | when | floor |
|---|---|---|
| `syncClock()` | the board was switched off | 15 |
| `tick()`, asleep | any time | 30 / 35 / 45, and the neglect check is skipped |
| `tick()`, awake | the board is left running | **none -- reaches zero** |

So leaving the board RUNNING overnight was punished where switching it OFF was
not, and an awake creature hit zero on everything in 100 minutes and could run
away at 160. The fix is `Pet::screenSleep()`: **turning the screen off with PWR
puts the creature to sleep**, so the sleep floors do the work rather than a new
rule in the drain. `sleep_test` simulates ten hours and fails if it comes back.

It needs BOTH the screen off and the night window (00:00-06:00), and both halves
were arrived at by getting it wrong first. A clock-only bedtime sends the
creature to bed while somebody is still playing with it. A screen-only rule
pauses the game every time the device is pocketed, and the creature is meant to
get hungry during the day. The rule is re-checked every tick rather than only on
the button, so a device put down at 23:00 goes to bed at midnight.

**Nothing wakes it at 06:00, deliberately.** An auto-wake reopens the hole this
exists to close: from the sleep floors, food is empty by 06:15 and every stat by
07:40, so anyone who sleeps past eight finds the creature ready to run away
again. It sleeps until the screen comes back on -- it wakes when the PLAYER
does. `sleep_test` puts the auto-wake back and reads `food=0 joy=0 ene=0 hyg=0`
at 09:00.

`isNightHour()` must keep working whether or not the window crosses midnight.
With `NIGHT_START 0` the plain `h >= START || h < END` is true for EVERY hour,
which put the creature to sleep the moment the screen went off at noon --
caught by `sleep_test` the same minute the window was narrowed.

A board whose RTC was never set reads months out and so never auto-sleeps. That
fails in the safe direction -- it keeps draining and the light button still
works -- and the clock is settable from SETTINGS or `RTCSET`.

The runaway deliberately does **not** ask for confirmation -- a pet you have to
authorise to leave is not at stake. A confirmation was added when this was first
reported and then removed: the bug was that a night's sleep could reach that
state at all, not that the ending was too easy to trigger.

### Adding a generation

Phase 0 is built (`feat/dex-expansion-phase0`): the guarantees that make the
data change provable.

- **`gen_dex_data.py --check`** now diffs the WHOLE committed table, not just
  Gen 1. Every entry below the old `DEX_COUNT` must come back byte-identical.
  Run it before and after any expansion; it reported the six missing
  cross-generation evolutions the first time it was pointed at 386.
- **`gen_dex_data.py --link`** fills in evolutions whose target has since joined
  the table, touching only rows whose committed value is 0. Re-run it after every
  expansion -- Sinnoh brings ELECTIVIRE, MAGMORTAR and RHYPERIOR needing it.
- **`tools/check_sprites.py`** reports art coverage per generation. Sinnoh and
  Kalos are 100%; Unova and Galar 91.7%, Paldea 85%. A species with no art
  anywhere stays in the dex at its own number -- dropping one renumbers
  everything after it -- but must be kept out of the egg pool.
- **`dexdata_test`** sweeps every species: names, typings, six non-zero base
  stats, evolutions that stay in range and terminate, learnsets that index real
  moves, regions tiling the dex exactly once, and both directions of the rarity
  invariant (an evolution target never hatches; an evolution-only species is
  always somebody's target, bar EEVEE's 134-136 which pet.cpp reaches by
  special case).

The nine species with no same-type attack are listed in that test rather than
tolerated, because 77 hand-picked moves against a new generation's typings is
exactly how a creature ends up unable to attack.

### EEVEE is the one evolution the table cannot express

`DexEntry.evolvesTo` is a single field, so only VAPOREON (134) is in the data.
The other seven live in `EEVEE_BRANCHES` in `dex_data.py`, which `gen_dex.py`
uses for TWO things at once: it emits `EEVEE_EVOS[]` into `dex.h` for the
firmware, and it feeds the rarity derivation that marks all eight
evolution-only. One source, so "can be reached by evolving" and "cannot hatch
from an egg" can never disagree.

They disagreed for six generations. The branch was hardcoded `134..136` in
`evolve()` AND again in `lineHasUnregistered()`, while `gen_dex.py` said
`evolved = {...} | {135, 136}` -- so ESPEON, UMBREON, LEAFEON, GLACEON and
SYLVEON were nobody's evolution target, came out as rare BASE forms, and hatched
straight from eggs. Eevee could never become any of them.

**A branch is filtered by `regionAvailable()` and `speciesHasArt()`**, via the
single `Pet::eeveeOptions()`. Without that a player with only the Kanto pack
could evolve an Eevee into UMBREON and own a creature that draws as a dex number
forever -- evolution is one-way. `eevee_test` proves it both ways: 40 evolutions
with only Kanto installed all stay in Kanto, AND with every pack it does reach
past Kanto, so the first check cannot pass by the branch being broken.

### Player-wide vs per-creature state

Badges (easy and hard), the avatar, the daily streak, the Pokedex bitmaps and
`totalMedals` belong to the PLAYER and outlive every pet. `newEgg()` must never
clear them -- `persist_test` proves they survive all three endings plus a
reload, so adding a reset there will now fail the suite rather than quietly
erase a run.

The one thing that does take them is `WIPE` (`factoryReset()` -> `prefs.clear()`),
which is a factory reset and is meant to.

They also have their own checkpoint pair, `plyA`/`plyB` -- see below.

### The save is CHECKPOINTED, and the legacy keys are no longer the truth

Read the header comment in `pet.cpp` before touching any of this. The short
version, because it is the most expensive bug this project has shipped:

**NVS writes one key at a time and `Preferences` commits on every `put()`.** A
save spread over ~50 keys is therefore not atomic. A power cut in the middle
leaves the early fields new and the later ones old, and what loads is a creature
assembled from two lives -- current Attack training beside older Defence, Speed
and species. That was reported as issue #3 and it was never a logic bug.

So each logical record is now ONE blob, CRC-guarded, written into TWO keys
alternately. The newest complete blob wins; if the write in flight never landed,
the previous one is whole.

| Record | Keys | Holds |
|---|---|---|
| the creature | `petA` / `petB` | species, age, IVs, training, moves, care, nickname, **and the pending party handover** |
| the player | `plyA` / `plyB` | trainer name, avatar, region, both badge ladders, the Pokedex bitmaps, streak, medals, records |

Four things about it that are easy to get wrong:

- **THE BODY IS APPEND-ONLY.** The CRC sits at the END, located by the record's
  own `size`, so a reader takes the prefix it understands and leaves its newer
  fields at their initialisers. Insert a field instead of appending one and you
  silently reinterpret every save on every device -- the same rule as move
  indices and the badge arrays (§ "wearing an INDEX"). If a change genuinely
  cannot be expressed that way, **bump the MAGIC, not the version**: that
  invalidates the record loudly instead of misreading it quietly. `version` is
  carried for diagnostics only.
- **Rejecting a checkpoint falls back to the legacy keys, which IS the torn-write
  path.** The first version of this demanded an exact size and version match, so
  the next field anybody added would have quietly reintroduced issue #3 on every
  device in the field. That is why the reader is tolerant and why
  `static_assert(offsetof(PetCoreSnapshot, pad0) == PET_FIXED - 1)` exists --
  failing it is the reminder to read the append-only rule, not to edit the
  number and move on.
- **The player record cannot be a struct.** `dexReg` is sized by `DEX_COUNT`,
  `eggByRegion` by `REGION_COUNT`, the badge arrays by `GYM_REGIONS`, and this
  project grows all three. A struct would move every field after whichever array
  grew. So the DIMENSIONS TRAVEL IN THE HEADER and each array is copied by its
  own prefix rule -- `loadBlob()`'s reasoning, applied inside one atomic blob.
- **One generation counter PER RECORD, never shared.** Slots alternate by the
  parity of the next generation, which is always `loaded + 1`, so the slot being
  written is always the opposite of the one loaded from -- the fallback copy can
  never be the one overwritten. Share a counter between the two records and that
  breaks the moment one write succeeds and the other does not.
  `powerloss_test` pins the invariant directly.

The ~50 legacy keys are STILL WRITTEN, deliberately: they are what `EXPORT`
carries and what a downgrade reads. But a failure in either checkpoint returns
before touching them, so the previous save stays the previous save rather than
becoming a half-updated one nothing can see is broken. **Dropping them is the
obvious next win** -- it would take a save from ~55 NVS commits to 2, cutting
both the tearing window and the page churn -- but it is a one-way door for
anyone who downgrades, so it is not in this change.

`powerloss_test` covers all of it, and every guard in it was negative-checked by
breaking the firmware on purpose. Note `poisonLegacy()` in that file: almost
every pet field is mirrored to a legacy key, so without it the fixture tests all
passed with the checkpoint reader disabled entirely -- § "A test that proves the
transcription rather than the firmware", caught again.

### NVS can run out, and the core's answer is to erase the whole partition

`nvs_flash_init()` returns `ESP_ERR_NVS_NO_FREE_PAGES` when the partition is
full, and `initArduino()` (`esp32-hal-misc.c`) responds by calling
`esp_partition_erase_range()` over the entire thing **before `setup()` runs**.
Every save on the device, gone, with nothing a player could see. Nothing in the
firmware could see it coming either: `putX()` returning short was discarded at
every call site except the checkpoints.

The stock `app3M_fat9M_16MB` table gives `nvs` 20 KB -- five 4 KB pages, ~630
entries of 32 bytes -- and NVS needs a free page to compact into. `nvsinfo.cpp`
reports headroom at boot and in the `HEALTH` heartbeat so the trend is visible
during a soak test. It cannot prevent the wipe.

**Do not "fix" this by growing `nvs` in the partition table.** `nvs` ends exactly
where `otadata` starts, so growing it moves `otadata`, `app0` and `app1`, and the
newly-added tail then contains leftover app bytes rather than erased flash --
which is the case that triggers the erase above. If more room is ever genuinely
needed, `ffat` (9.875 MB at `0x610000`) is **unused** -- the sprite packs are on
a real SD card via `SD_MMC`, not on that partition -- and it sits after both app
slots, so it can be shrunk and a new partition added in the freed tail without
moving a single existing offset. Measure with `nvs_get_stats` first; fewer writes
per save is the real fix.

### Losing power is not the only way to lose a save

- **A 4-second hold on PWR is a HARDWARE power-off inside the AXP2101.** The
  rails drop and the firmware is never told, so it cannot save on shutdown. What
  it can do is take the PMU's long-press interrupt, which fires at the
  long-press threshold and therefore BEFORE the off threshold. `pwrLongPressed()`
  and `batLowWarning()` both route into `flushBeforePowerLoss()`. **The lead time
  is the PMU's, not ours -- measure it on hardware.** `XPOWERS_POWEROFF_6S`
  widens it if 4 s turns out too tight.
- The PMU **latches** its interrupts and one register read reports all of them,
  so there can be only ONE poller: `pwrPoll()`, once per loop, which clears
  unconditionally. The old code cleared only when it had seen a short press,
  which was harmless while that was the only enabled interrupt and would have
  left every new bit latched forever.
- `flushBeforePowerLoss()` is deliberately the single entry point for all three
  callers, because the one case where flushing must NOT happen -- a `WIPE`
  waiting to restart, where the shutdown handler would write the deleted
  creature straight back -- has to be impossible for the next caller to forget.
  An earlier attempt put that guard on `Pet` instead and broke `console_test`,
  which wipes the shared global pet and keeps using it.
- **The party handover used to live only in RAM.** `update()` hands the creature
  over and `newEgg()` saves the egg immediately, but the party write waits for
  the player to accept a slot -- a banner or a whole chooser screen later. A
  power cut in that window lost the creature with the save that erased it already
  committed. It is in the pet checkpoint now, so the egg and the creature it
  replaced commit together. Go through `clearEnded()` / `setEnded()`, never
  `pet.endedKind = ...`.
- **`Pet::saveHealthy()` is false when NVS would not open or has refused three
  writes in a row**, and `drawSaveWarning()` puts a red triangle beside the
  battery. A player who knows saving is broken can `EXPORT`; one who does not
  loses the week. It is not a dialog on purpose: the condition persists, so a
  modal would be dismissed once and forgotten.

### Tests

```bash
bash tools/emu/tests/run.sh          # all 34 suites
bash tools/emu/tests/run.sh battle   # just matching ones
```

They compile the REAL sources against the emulator stubs, so they assert against
`Pet`/`Party`/`Combatant` themselves rather than restating their rules. Two exist
because nothing else can catch what they catch:

- `flush_test` -- a screen with no `gfx->flush()` leaves the panel frozen, and
  screenshots are structurally blind to it: `--shot` reads `gfx->buffer()`
  directly and never consults `frameReady`. The training submenu shipped frozen
  exactly this way.
- `i18n_test` -- `STRINGS` is positional, so a short language row is zero-padded
  by the compiler with no diagnostic, shifting every later string in that
  language only.

Run them after touching `pet.cpp`, `battle.cpp`, `i18n.cpp` or any render path.

**`swipe_test` exists because the same bug was shipped four times** -- the move
picker, the player card, the gym list and the box each closed on a horizontal
swipe instead of paging, and each was found by hand rather than by a test. It
now drives `onSwipe(-1)` against every paged screen and asserts the page
advanced AND the screen stayed open. **Any new paged screen must be added to
it**, or this will happen a fifth time. See § "Traps that have caught us more
than once" -- this is case 1 of that pattern, not a one-off.

It also drives `learnableFor()`, because the move picker having its own copy of
the TM gate is the same mistake wearing a different hat.

The **region chooser** is paged too (three rows a page, four regions and
growing) and is in there as well. It is the one screen that WRAPS rather than
closing off the end -- it is the root of its own screen and at first boot there
is nowhere to go back to -- and `rpickSwipe()` must be checked FIRST in
`onSwipe()`, because the starter screen's `if (pet.awaitingStarter()) return;`
would otherwise swallow the gesture and make the first-boot chooser unpageable.

### Battle system — decided, not started

**Turn-based and move-based, like the real games.** This is not a fresh choice:
`moves.h` (78 moves: name, type, MC_PHYS/SPEC/STATUS, power, acc, effect, target,
plus stat stages, priority, multi-hit, recoil, drain, heal, charge/recharge) is
already a turn-based engine's data layer. `types.h` has integer `typeEffPct()`.
The old roadmap line about "resolution by ATK/DEF/SPD" is superseded — it would
discard all of it.

Settled:

- **Special split lives on the species, not the individual.** `dex_stats.py`
  already holds all 6 stats; `gen_dex.py:94` unpacks `spa, spd` and discards
  them because the struct on line 71 declares only 4. Fix = add `bSpa`/`bSpd`
  and regenerate. Special attack runs off `ivAtk/trAtk` vs `bSpA`, special
  defence off `ivDef/trDef` vs `bSpD` — **no new IVs, no NVS migration** (the
  rationale is already written up in `fetch_pokeapi.py:13-17`).
- **The party is the battle team.** `PartyMon` already carries full stats and
  `Party::atkOf/defOf/speOf/vitOf` exist. Retired pets are frozen at banking
  (level, training — and moves, once they exist), which is the level cap.
- **Moves are player-chosen**, and editable on a banked creature too. On
  level-up the player picks which of the 4 to forget; it is never automatic.
  Moves were originally FROZEN at banking to give the farewell weight, but that
  left a creature banked with a poor set useless forever -- which fights hard
  mode, where coverage decides the run. A banked one is edited from its party
  sheet, and is limited to what it could have learned at its frozen level.
- **Status ailments are IN.** Requires a `MoveEntry` schema change: `effect` is
  a single slot already used by EF_RECOIL etc., so a damaging move cannot also
  carry a secondary status. Add `ailment` + `ailChance` fields, then author them
  onto `dex_moves.py` (hand-written, not fetched — PokeAPI ailment data was
  never pulled).
- **No PP.** No field in `MoveEntry`, and it stays that way.
- **Rewards are badges/rank, not XP.** `level() = 1 + ageMinutes/MINUTES_PER_LEVEL`
  — level is age. Granting XP would break real-time ageing. Learnsets are
  level-keyed, so moves unlock as the pet ages.

- **Ailments are battle-only.** They live in the battle state and clear when it
  ends — never in `Pet`, never saved, never ticked by offline catch-up.

**Endgame: 8 gym leaders + Elite 4, on two difficulties.** Easy is the ladder;
hard reruns it with better AI decision-making and opponents with strong IVs and
real movesets. Needs a trainer roster table (~13 trainers x 3-6 mons: species,
level, moves, IVs) and two AI tiers — easy picks naively, hard reads
`typeEffPct()`, STAB, stat stages and available KOs.

Level anchor for balancing the ladder: `MINUTES_PER_LEVEL 60` and farewell at
3 days means a fully-raised pet retires at **level 73**. Pets banked earlier are
weaker, so team strength reflects how long each one was raised.

- **The player picks the battle team.** Pool = the live pet plus the 6 banked;
  choose up to 6 per battle. The live pet is selectable, never compulsory.
  Battling costs the *live* pet energy (banked pets are retired, so they cost
  nothing) — it ties battle to the care sim and rate-limits grinding without a
  cooldown timer.
- **The ladder is sequential** (this REVERSES the earlier "no gating, attrition
  is the gate" rule). A leader opens once the previous is beaten, tracked
  separately per difficulty so hard mode is its own run. The original rule was
  written before both ladders were level-capped; once they were, nothing stopped
  you opening on Lance and simply losing, which reads as a dead end rather than
  a challenge. Attrition still does the work WITHIN a fight.
- **Level caps at 100** (`MAX_LEVEL`), reached at 4d 3h. `MINUTES_PER_LEVEL`
  stays 60 — compressing the curve to force 100 into a 3-day life would be a
  balance change that buys a number you can already reach by playing on.

Phase 1 turned out to be **already done**: `dex.h` has had `bSpA`/`bSpD` for all
151 all along. Only the accessors were missing (`Pet::spaStat/spdStat`,
`Party::spaOf/spdOf`) — added, so damage maths is unblocked.

Phase order: (1) ~~special stat accessors~~ · (2) ~~move storage on `Pet` +
`PartyMon`~~ · (3) ~~moveset UI~~ -- all **done**. Next: (4) `MoveEntry` ailment
~~fields~~ **done** · (5) damage + turn resolution, headless-testable in the
~~emulator~~ **done** (`battle.h`/`battle.cpp`) · (6) battle UI ·
~~(6) battle UI~~ · ~~(7) trainer roster + gyms + Elite 4~~ -- **done**.
Next: (8) hard mode AI, and a team-select screen.

The ladder uses the real FireRed/LeafGreen teams and levels, unrescaled, and
they land almost perfectly on this game's curve. Measured solo win-rate for one
perfect-IV Charizard, 40 runs each:

| your level | gyms 1-4 | gyms 5-7 | Giovanni | Elite 4 | Champion |
|---|---|---|---|---|---|
| 40  | 90-100% | 0%      | 0%     | 0%     | 0%  |
| 60  | 97-100% | 50-75%  | 0%     | 0-15%  | 0%  |
| 73 (a full 3-day life) | 100% | 82-97% | 7% | 0-32% | 0% |
| 100 | 100%    | 100%    | 52%    | 55-92% | 42% |

So one creature clears the eight gyms over a normal life and still cannot take
the Elite 4 -- exactly what "no gating, attrition is the gate" was meant to do.
A banked team is the answer, which makes farewells matter.

Team-select is NOT built: the squad is the live pet plus the first five banked
members in order. It only bites when you hold 7 candidates.

### Hard mode (designed, not built)

Not "the AI cheats". Hard mode **removes overlevelling as a strategy** so the
fight is about type matchups, movesets and decisions:

- **Team size is capped to the opponent's.** Brock brings 2, so you bring 2.
- **Levels are capped to the opponent's highest.** A level 100 creature fights
  Brock at 14.
- Opponents roll `HARD_IV` (31) instead of `EASY_IV` (16), already in trainers.h.
- The AI actually chooses (see phase 8) instead of `random()`.

Both caps are applied when building the squad, not to the stored creature --
nothing is written back, exactly like ailments. `badgesHard` already exists on
Pet and is tracked separately from `badges`.

### Peer-to-peer battles (designed, not built)

The S3 has WiFi and BLE; neither is currently brought up anywhere in the
firmware. **ESP-NOW** is the fit: peer-to-peer, no router, ~250-byte payloads,
and a `Combatant` is only ~40 bytes.

**One device is authoritative.** The host owns the whole battle state and runs
`battleAct()`; the guest only sends a move index and renders what it is told.
This is not a preference -- `battle.cpp` makes **11 `random()` calls per turn**
(crit, damage spread, accuracy, ailment procs, multi-hit count, confusion,
thaw, wake, speed ties). Two devices resolving independently desync inside a
single turn, and a shared seed only papers over it until the builds differ by
one `random()` call. Sending resolved outcomes cannot drift.

Wire format, roughly:

1. `HELLO` -- firmware version + protocol version. Refuse a mismatch loudly;
   a silent desync is far worse than a refusal.
2. `SQUAD` -- each side sends its team as `Combatant`s (dex, level, the five
   stats, 4 moves, name). ~40 bytes each, up to 6.
3. Per turn: guest sends `MOVE <slot>`; host resolves and replies with the
   `TurnLog`s plus both HP/ailment states. `TurnLog` already carries everything
   needed to narrate, which is why the guest needs no game logic at all.
4. `END` -- winner.

Costs to weigh before starting: the WiFi stack is ~40-50 KB RAM (there is
headroom -- currently 10% used), meaningful current draw on a battery device,
and pairing UX on a touch-only screen.

### Box size (if the party grows past 6)

`sizeof(PartyMon)` is **48 bytes** now (it was 30 when this was written; `moves[]`
and then the care block were appended) and the NVS partition is 20 KB (`0x5000`).
So the arithmetic has moved: the current box of 18 is 864 bytes, a box of 80 is
3840 and still inside the ~4000-byte single-blob figure, and 100 (4800) already
exceeds it and would need splitting across two keys. **Derive these from
`sizeof(PartyMon)` when the time comes rather than trusting the numbers in this
paragraph** -- they have been wrong once already, which is § "sizeof(PartyMon) is
load-bearing in four places" wearing a doc instead of code. RAM is a non-issue.

Check the headroom too, not just the blob limit: see § "NVS can run out". A box
that fits one blob can still be the thing that fills the partition.

**Fix the migration first.** `Party::begin()` infers the old record size as
`stored / PARTY_SLOTS`, which is right when the stride grows but wrong when the
slot count does: 180 stored bytes over 30 slots would infer a 6-byte record and
destroy the party. Key it off `sizeof(PartyMon)` before changing PARTY_SLOTS.

Note on (6): the battle screen is a 2x2 move grid, not four stacked rows --
the round panel has to fit both creatures, both HP bars and the menu. The only
way into a battle right now is the serial command `BATTLE <dex> [level]`; it
gets a real home in (7). The foe is built through `Pet` so it uses the same stat
formula and the same learnset-driven moveset as the player, rather than
special-cased enemy maths that could quietly diverge. Foe move choice is
`random()` for now -- that IS phase 8.

**Fight length was checked and is NOT a problem** (an earlier note here claimed
otherwise; it was wrong). 240 simulated L50 fights average 3.1 turns, and that
matches the real games: a 1v1 at equal level there is also 3-5 turns. HP already
lands exactly on the canonical value (Charizard L50 = 153 both ways), and while
the other stats sit ~40% high -- the deliberate deviation `calcStat()` documents,
so newborns do not show single digits -- damage depends on the A/D *ratio*, which
is preserved: 1.02 ours vs 1.03 real. Do not "fix" this; it would make combat
less faithful, not more. Length in a gym comes from fighting six in a row.

Note on (4): `MoveEntry` gained `ailment` + `ailChance`, and the pair is
OPTIONAL in `dex_moves.py` -- only the 17 moves that inflict one spell it out,
`gen_moves.py:unpack()` defaults the rest. Adding an ailment is a one-line edit.
There is no dedicated status move (no THUNDER WAVE, no SLEEP POWDER), so
ailments ride as secondary chances on damaging moves. `AIL_SLEEP` exists in the
enum but nothing inflicts it yet -- adding TOXIC/THUNDER WAVE/SLEEP POWDER would
mean new rows in `dex_moves.py` plus a re-run of `fetch_pokeapi.py`, which is now
cheap since `tools/pokeapi_cache/` is warm.

Note on (3): there is no level-up "you learned a move" prompt, and there should
not be -- 1907 of 2281 learnset entries are level 0, so it would almost never
fire. The moveset is edited on demand from card page 4 instead, which is both
simpler and closer to what was asked for.

### Training mechanics — deliberately unresolved

Training already exists and is already EV-shaped: `trAtk/trDef/trSpe` (`pet.h:47`)
capped by `trMaxFor(iv) = 70 + 30*iv/31`, feeding `calcStat()`. ATK trains via the
punching bag, SPE via the ball game, DEF passively (+1 per `DEF_TRAIN_TICKS` = 60
min of good wellbeing).

~~Open question: DEF has no active trainer~~ **settled** -- kept passive and
styled for it. `renderTrain()` draws the DEF row flat in `UI_TRACK` and the tap
handler skips it (`passive = (i == 2)`), so it reads as information rather than
a dead button. DEF still trains by itself, +1 per `DEF_TRAIN_TICKS` of good
wellbeing.

### Gesture map -- DONE, do not re-plan it

Three axes, one meaning each. This replaced a per-screen grab-bag in v3.13 and
is recorded here as fact so nobody redesigns it from the old notes.

| Gesture | Means | Where |
|---|---|---|
| Horizontal | move along the TILE AXIS | `PLAYER . PARTY . [PET] . EXPLORE . GYM . DEX` |
| Up | deeper (the pet's card, a sheet) | everywhere |
| Down | BACK, one level | everywhere |
| Rim drag | PAGE the current screen | every paged screen |

**The horizontal axis BUMPS at both ends and can no longer close anything.**
That is the entire point. Paging and exiting used to be the SAME gesture -- a
screen paged until you ran off the end, at which point it closed -- which is
case 1 of § "Traps" and shipped four times. Paging moved to the rim
(`onRim`, `uiRimTarget`) precisely so the two can never be confused again.

**`uiRimTarget()` is the single table of paged screens**: which variable holds
the page and how many there are. The arc scrollbar, the rim drag and
`swipe_test` all read it, so a new paged screen gets its scrollbar, its gesture
and its coverage from one edit. Adding a screen to `onSwipe` instead is the old
mistake wearing new clothes.

**The tile order is not arbitrary.** `PLAYER . PARTY . [PET] . EXPLORE . GYM .
DEX` keeps yours on the left and the world on the right. Explore is beside the
pet because it is a primary loop, not an action owned by the gym ladder. The pet
screen names both neighboring destinations so the horizontal axis is visible
before somebody already knows to swipe.

**Both multi-region tiles still open on their CHOOSER**, and `swipe_test` still
asserts it. Down backs out of a ladder to the chooser and out of the chooser to
the pet; up keeps the region shortcut it always had.

`uiChrome()` draws the axis dots and the arc scrollbar, and every renderer that
is on the axis or pages calls it immediately before `gfx->flush()`. A screen
either has the chrome or visibly does not.

### Round-panel geometry: ask, never guess

`uiSafeHalfWidth(y)` is the half-chord at row y, and `uiSafeHalfWidthFor(y0,y1)`
the narrower of two -- a rounded box is widest at its middle but its CORNERS are
what leave the circle. Every full-width layout asks these. The gym ladder used
to draw five identical 326 px rows over y 110..374 where the real chord runs
448 down to 372: wasting 66 px in the middle AND hanging the end rows' corners
off the glass, both at once.

**`UI_TAP_MIN` is 44 and is a HARD FLOOR, not a target.** It is 44 *points*
borrowed as pixels: this panel is 466 px over 1.75 in = 266 ppi, so a pixel is
0.095 mm and 44 px is 4.2 mm -- under half a fingertip. It survives only because
several laid-out screens cannot grow without being redesigned. **New work uses
`UI_TAP_FINGER` (94 px = 9 mm)**, and `hit_test` holds `uiButtonHeights()` to it.

That single mis-scaled constant is behind all three "hard to hit" reports. The
home icons were 60 px (5.7 mm) with a 6 px gap; they are 80 px now, which is
only possible because the four care bars moved to the rim as arcs and gave the
bottom of the panel back. Four `UI_TAP_FINGER` targets in one row would reach
radius 222 of 233 -- so a finger-safe home row on this panel is three icons, not
four, and 80 px is the honest compromise that keeps all four.

**The party and box are a RING of six, not a 2x3 grid** (`partySlotPos`,
`partySlotAt`, `partyHubAt`). A grid on a circle spends its corners on glass
that is not there; the old 150x70 cells were 70 px tall, under the floor, with
their outer corners at the bezel. The BOX button is the hub in the middle,
because the centre of a round panel is the easiest place on it to hit.
`hit_test` MEASURES the slot radius off `partySlotAt()` rather than reading
`PSLOT_R`, so it checks the hit areas the firmware actually answers with.

