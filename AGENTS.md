# AGENTS.md — TamaPoke Engineering, Save-Safety, PR & Release Rules

Pokémon-inspired tamagotchi firmware for the **Waveshare ESP32-S3-Touch-AMOLED-1.75** (round 466×466 CO5300 QSPI AMOLED, CST9217 I2C touch, PCF85063 RTC, AXP2101 PMU, ES8311 I2S audio, microSD).  
Personal, non-commercial fan project. Code is MIT; sprites are CC BY-NC (PMD SpriteCollab).

---

## 1. Repository Layout (`v4.4.0+`)

| Path | Responsibility |
|---|---|
| `TamaPoke.ino` | Main sketch: UI screens, 466×466 rendering, touch/rim/swipe gesture routing, serial console, `FW_VERSION` |
| `pet.cpp` / `pet.h` | Live companion & player state machine: needs, real/level age, evolution (including branching evolutions), eggs, daily bond, medals, `petA`/`petB` & `plyA`/`plyB` NVS checkpoints |
| `party.cpp` / `party.h` / `collection_store.h` | 6-slot Party + 1,200-slot Box in PSRAM, persisted as alternating `KBC3` (`cbA`/`cbB`) checkpoints in the dedicated `tpcollect` partition |
| `save.cpp` / `save.h` / `ckpt.h` | Serial `EXPORT` / `IMPORT` (`.tpsave` v3 format), staged atomic restore, CRC-16/CCITT-FALSE verification |
| `species.h` / `dex.h` / `types.h` | 1,025 species across 9 regions (Kanto–Paldea), base stats, typings, 18×18 type chart, evolution chains (`EVO_BRANCHES`), UI palette |
| `moves.h` / `move_names.h` / `battle.cpp` / `trainers.h` | Turn-based battle engine, learnsets, 7 regional gym ladders + Elite 4 (Easy & Hard), battle AI |
| `wild.cpp` / `wild.h` / `inventory.cpp` / `items.h` | Wild encounters, capture mechanics, field & battle bag items |
| `link.cpp` / `link.h` / `linknow.cpp` | Peer-to-peer ESP-NOW LAN battles (`LINK_PROTO`) |
| `i18n.cpp` / `i18n.h` | 7-language positional UI string table (`ES`, `EN`, `FR`, `DE`, `IT`, `PT`, `NL`) |
| `audio.cpp` / `gbsynth.cpp` / `music.h` / `cry.h` | Game Boy pulse/noise chiptune synth & species cries over ES8311 I2S |
| `rtcbat.cpp` / `sdmon.cpp` / `nvsinfo.cpp` / `pin_config.h` | PCF85063 RTC, AXP2101 battery/PWR button, microSD sprite streaming & USB `PUT` transfer, NVS headroom telemetry |
| `tools/` | Asset generators, `build_web.sh`, `make_screens.sh`, `unpack_bundle.py`, and release/installer validators (`check_release.py`, `check_installer.py`, `check_savefile.mjs`, `check_packs.mjs`) |
| `tools/emu/` | Desktop SDL2 & headless emulator (`tamapoke-emu`) + host and native ESP32-S3 (`esp-emu`) test suites (`tools/emu/tests/run.sh`) |
| `web/` | Browser flasher (`esp-web-tools` vendored), save backup/restore vault (`savefile.js`), sprite pack uploader (`packs.js`), and committed `sprites-<region>.pak` bundles |

---

## 2. Mandatory PR / MR Screenshot Rule

**Every PR/MR that changes any UI, layout, text, icon, colour, or background MUST include rendered 466×466 screenshot previews in the PR description (or a PR comment) so the reviewer can inspect the change visually on GitHub.**

### How to generate real-sprite screenshots
1. **Unpack the committed sprite bundles once per checkout:**
   ```bash
   python3 tools/unpack_bundle.py
   ```
   - `tools/sdcard/mons/*.bin` is gitignored. Without unpacking first, the emulator draws `? No sprites` or bare dex numbers instead of Pokémon sprites. Never commit or post art-less screenshots.
2. **Build the desktop emulator and render headless shots:**
   ```bash
   bash tools/emu/build.sh
   tools/emu/tamapoke-emu --wipe --shot main --lvl 36 --iv 25 --dex 6 --out /tmp/shot.ppm
   ```
   - Or run `bash tools/make_screens.sh` to regenerate the full `docs/screens/*.png` set when updating shared screens.
3. **Commit updated `docs/screens/*.png` on the PR branch and embed the raw GitHub image URL in the PR body:**
   ```markdown
   ### Screenshot Preview
   ![Preview](https://raw.githubusercontent.com/eperdeme/TamaPoke/<branch-name>/docs/screens/<name>.png)
   ```
4. **Keep the three screenshot lists in sync** whenever adding or renaming a documented screen:
   - `SHOTS` in `tools/make_screens.sh`
   - `--shot` handlers in `tools/emu/main_sdl.cpp`
   - `<img src="docs/screens/...">` tags in `README.md`

---

## 3. Zero-Data-Loss Save & Upgrade Safety Rules (CRITICAL)

**A firmware update or backup/restore must NEVER lose or corrupt a player's active companion, 1,200-slot Box, Party, Pokédex, Badges, Bag, or Settings.**

### 3.1 Dual Checkpoints are Append-Only (`petA`/`petB`, `plyA`/`plyB`, `cbA`/`cbB`)
NVS writes one key at a time, so multi-key saves tear on power loss. All critical state lives in CRC-guarded alternating A/B blobs where the newest intact generation wins and the older slot remains untouched as the fallback:

| Store | Partition / Namespace | Keys | Contents |
|---|---|---|---|
| **Live Creature** | `nvs` / `tamapoke` | `petA`, `petB` (`TKP1`) | Species, age, real age, IVs, training, moves, care stats, nickname, origin, pending handover |
| **Player Progress** | `nvs` / `tamapoke` | `plyA`, `plyB` (`TKY1`) | Trainer name, avatar, region, easy/hard badges, Pokédex bitsets (`dexReg`, `dexShinyReg`, `dexHatched`, `dexShinyCaught`, `dexCaught`, `dexHatchedShiny`), streak, medals, records |
| **Party & 1,200-Slot Box** | `tpcollect` / `tpcol` | `cbA`, `cbB` (`KBC3`) | 6 `PartyMon` party slots + 1,200 `PartyMon` box slots |

**Rules when modifying saved state:**
1. **Never insert or reorder fields inside an existing checkpoint.** The CRC sits at the end (located via `hdr.size`), and readers prefix-copy the bytes they understand.
   - For `PetCoreSnapshot` (`pet.h` / `pet.cpp`): keep `static_assert(offsetof(PetCoreSnapshot, originKind) == PET_FIXED - 1)` intact. Any new data goes in unused validated padding or as a magic-tagged tail appended after `"BDAY"` / `"RAGE"`.
   - For `Player` (`pet.cpp`): array dimensions travel in the header; new arrays or fields must be appended at the very end of `savePlayerSnapshot()` / `loadPlayerSnapshot()` with bounds-checked prefix loading so shorter older saves zero-fill the new tail.
   - For `PartyMon` (`party.h` / `party.cpp` / `collection_store.h`): `sizeof(PartyMon)` and `PARTYMON_STATE_VERSION` are load-bearing. Either use unused padding bytes with a two-byte magic/complement signature (e.g., `origin` + `originChk`) or append to `PartyMon` and migrate shorter record strides via prefix `memcpy` in `Party::loadCollection()`. Never default a new field to a non-zero value that claims migrated records have data they do not have.
2. **Never erase `tpcollect` or `nvs` on an initialization or validation failure.** If a newer save format is encountered, block writes rather than overwriting with empty data.
3. **Keep large serialization buffers off the stack.** The FreeRTOS `loopTask` stack is only 8 KiB. Any buffer ≥256 B used during save/load/export/import must use `saveAlloc()` (PSRAM-first) or heap allocation.

### 3.2 Three-Place Sync for Every New NVS Key
Every new NVS key (e.g., a new setting) must be additive (missing key = safe default) and registered in all three places so `.tpsave` backups carry it:
1. `SAVE_FIELDS` in `save.cpp`
2. `GAME_FIELDS` in `web/savefile.js` *(note: `web/savefile.js` and `web/packs.js` must never reference `window`, `document`, or `navigator` so Node can import them)*
3. `tools/check_savefile.mjs`

Run `node tools/check_savefile.mjs` and `node tools/check_packs.mjs` after any save or web change.

### 3.3 Append-Only Indices (Species, Moves, Strings)
- **Species (`DEX_TBL`):** Stored raw as `int16_t` (`1..DEX_COUNT`). Never use `uint8_t` for a dex number (overflows above 255), never hardcode `151` or `386`, and never renumber existing species.
- **Moves (`MOVE_TBL` in `tools/dex_moves.py`):** Move indices are stored raw in `Pet::moves[]` and `PartyMon::moves[]`. Never insert a move into the middle of `MOVES`; always append new moves at the end under `APPEND-ONLY BELOW HERE`.
- **UI Strings (`StrId` in `i18n.h` / `i18n.cpp`):** `STRINGS[LANG_COUNT][STR_COUNT]` is positional across all 7 languages (`ES`, `EN`, `FR`, `DE`, `IT`, `PT`, `NL`). Adding a string requires adding its enum to `i18n.h` and its translation at the exact same position in all 7 language rows.

### 3.4 Partition Table & Web Installer Blast Radius
- **Never move existing partition offsets in `partitions.csv`:**
  - `nvs` (`0x009000..0x00E000`), `ffat` (`0x610000..0xF50000`), `tpstate` (`0xF50000..0xF70000`), and `tpcollect` (`0xF70000..0xFF0000`) must stay out of the firmware flash blast radius (`0x000000..0x610000`).
- **Never set `"new_install_prompt_erase"` to `false`** in `web/manifest.json`, and never replace the 4-part manifest (`bootloader.bin`, `partitions.bin`, `boot_app0.bin`, `app.bin`) with a single merged binary at `0x0` (which would overwrite `nvs` at `0x9000`).
- **Keep `esp-web-tools` vendored** under `web/vendor/` so `tools/check_installer.py` can verify the default-unchecked erase behavior.

---

## 4. Step-by-Step Release Procedure

All GitHub CLI commands in this repository **must** pass `-R eperdeme/TamaPoke` (otherwise `gh` defaults to the upstream `DylanPDao/TamaPoke` remote).

1. **Develop & verify on a feature branch:**
   - Never commit directly to `main`.
   - Open a PR with rendered screenshots (§2) and wait for `Firmware tests` CI to pass.
   - Merge the PR into `main` and update local `main` (`git checkout main && git pull origin main`).
2. **Bump the version to `X.Y.Z`:**
   - `TamaPoke.ino`: `#define FW_VERSION "X.Y.Z"`
   - `README.md`: `![Firmware](https://img.shields.io/badge/firmware-vX.Y.Z-8A2BE2)` (also update `README.md` § *Game manual (the actual numbers)* if any balance constants changed).
3. **Write `docs/release-notes/vX.Y.Z.md`:**
   - Required by `tools/check_release.py` (must exist, be ≥200 bytes, and contain newlines) and displayed directly on the web installer page via `web/installer.js`.
   - **Write for players, not developers:** explain what changed, what was fixed, and include the save-upgrade safety footer (download a backup first, leave **Erase device** unchecked, keep power connected). Do not mention internal file or function names.
4. **Rebuild the web installer assets (AFTER the last source edit):**
   ```bash
   bash tools/build_web.sh
   ```
   - Compiles the ESP32-S3 firmware with `arduino-cli`, runs the compiled partition contract check, writes the 4 split binaries + merged `tamapoke.bin` to `web/firmware/`, updates `web/manifest.json` to `X.Y.Z`, regenerates `web/dex.json` and `web/paks.json`, stamps SHA-256 `?v=` cache keys into `web/installer.js` and `web/index.html`, and runs `tools/check_installer.py`, `tools/check_savefile.mjs`, and `tools/check_packs.mjs`.
5. **Run the release consistency check:**
   ```bash
   python3 tools/check_release.py
   ```
6. **Commit and push `main` BEFORE tagging:**
   ```bash
   git add README.md TamaPoke.ino docs/release-notes/vX.Y.Z.md web/
   git commit -m "Rebuild installer for X.Y.Z and add release notes"
   git push origin main
   ```
   - `tools/check_release.py` enforces that any release tag is an ancestor of `origin/main`.
7. **Tag `vX.Y.Z` on `main` and push the tag:**
   ```bash
   git tag vX.Y.Z
   python3 tools/check_release.py vX.Y.Z
   git push origin vX.Y.Z
   ```
8. **Automated GitHub Release publication:**
   - Pushing tag `vX.Y.Z` triggers `.github/workflows/publish-release.yml`, which runs the full `firmware-tests` workflow, runs `python3 tools/check_release.py vX.Y.Z`, and publishes the GitHub Release using `--notes-file docs/release-notes/vX.Y.Z.md`.
   - Monitor the workflow run (`gh run list -R eperdeme/TamaPoke --workflow "Publish firmware release"`) rather than calling `gh release create` manually.

---

## 5. UI/UX, Hardware & Testing Rules

1. **Single source of truth for rules and geometry:**
   - Never copy-paste a gameplay gate or layout formula into a second caller or a test. Expose one helper (`moveUnlockLevel()`, `uiButtonDisabled()`, `uiMenuRowCenterY()`, `uiSafeHalfWidthFor()`, etc.) and have drawing, touch handling, and tests call it.
2. **Round 466×466 AMOLED & Finger Ergonomics:**
   - The panel is a 466×466 circle (`CX = 233, CY = 233, R = 233`, ~10.5 px/mm). Check chord widths with `uiSafeHalfWidthFor(y0, y1)` so rounded-rect corners never clip off the bezel.
   - Primary touch targets must be at least `UI_TAP_MIN` (`44 px`) tall, and preferably `UI_TAP_FINGER` (`94 px` / 9 mm) where space allows (`hit_test.cpp` enforces both).
   - **No hair-trigger exit traps (#24):** Do not make unmarked top headers or empty dead space between buttons/rows close a screen. Rely on swipe-down and explicit `BACK` / `CLOSE` targets (`y > 380` or dedicated button) so a slight mis-tap never loses the player's place.
   - **No pet-screen long-press retire (#24):** Releasing/retiring a companion lives only in `MENU -> RETIRE`.
3. **Every render path must flush (`flush_test`):**
   - Headless `--shot` reads `gfx->buffer()` directly and cannot detect a missing `gfx->flush()`. Every screen state must pass `flush_test`.
4. **Always verify with `arduino-cli` AND `tools/emu/tests/run.sh`:**
   - `tools/emu/build.sh` auto-generates `proto.h` forward declarations, so code that compiles in the emulator can fail in `arduino-cli` if a function is used before declaration.
   - Negative-check new tests by temporarily breaking the guarded behavior and watching the test fail. Start tests from a clean, explicit state rather than relying on state left behind by earlier assertions.
