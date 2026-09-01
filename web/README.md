# TamaPoke web installer

A one-click page that flashes the firmware and loads the sprites from the browser
(Chrome/Edge), with no Arduino or drivers. It uses
[ESP Web Tools](https://esphome.github.io/esp-web-tools/) to flash and **Web
Serial** to push the sprites to the SD with the firmware's `PUT` protocol (the
same one as `tools/send_sd.py`).

## Contents

- `index.html` — the installer and board-manager shell.
- `style.css` / `installer.js` — responsive UI, Web Serial transport, pack
   reconciliation, and save backup/restore.
- `editions.json` — the GitHub repository queried for Releases plus the fallback
   description for the current Pages build.
- `manifest.json` — the Standard edition's ESP Web Tools config.
- `paks.json` — generated region sizes, CRC32s, file counts, and firmware region
   indices.
- `firmware/bootloader.bin` `partitions.bin` `boot_app0.bin` `app.bin` — what the
  installer actually writes, each at its own offset. **This is deliberate**: a
  single image at `0x0` pads the gaps with `0xFF` and so blanks the NVS
  partition at `0x9000`, which is the player's save. `tools/check_installer.py`
  fails the build if anything the manifest ships would land on it.
- `firmware/tamapoke.bin` — the same four merged into one image for flashing a
  **blank** board from the command line. Not in the manifest, because installing
  it over an existing game erases the pet.
- `sprites-<region>.pak` — the sprites bundled (TPAK) **one file per region**, so
   the page sends a region in one click. They are **generated** by
  `tools/pack_bundle.py`, which derives the region list from `dex_data.py`, and
  **committed** — see *Hosting the sprites* below for why they have to be.

## Regenerate

After changing the firmware or the sprites:

```bash
bash tools/build_web.sh        # recompiles -> firmware/tamapoke.bin AND rebuilds the region .paks
```

## Test locally

Web Serial and ESP Web Tools need a **secure context**: `https://` or
`http://localhost`. To test:

```bash
python3 -m http.server 8000
# open http://localhost:8000/web/ in Chrome/Edge
```

## End-user flow

1. Pick a released firmware version and **Install firmware**. Pick the USB port; only
   tick "Erase device" for a fresh board.
2. **Connect board**. The page compares the installed SD marker for every region
   against the CRC in `paks.json` and labels it Current, Update available,
   Installed / unversioned, or Not installed.
3. **Select needed** excludes current packs. Install the remaining choices over
   USB (progress bar, ~8–10 min per 40 MB). Close the step-1 install dialog
   first: only one program can use the port at a time.
4. Use **Download save** before an upgrade. **Restore save** accepts that same
   `.tpsave`, streams its `IMPORT` commands with flow control, and lets the
   firmware validate the whole checksum before NVS is changed.
5. Restart (PWR button) after changing packs so thumbnails are reloaded.

The custom-file option lets advanced users send their own `.bin`. Because those
files did not come from a known bundle, firmware invalidates the affected pack
marker and the page reports it as unversioned on the next refresh.

## Firmware versions and GitHub Releases

The version selector queries the public GitHub Releases API for the repository
named in `editions.json`. A Release supplies the version name, date, channel,
link, and changelog body. The bytes do not come from Release attachments:
GitHub's attachment downloads have no browser CORS header. Instead, each option
uses the immutable `web/manifest.json` and four firmware parts committed at that
Release's tag through `raw.githubusercontent.com`, which does allow browser
fetches.

The page validates a release before showing it. Its tag must match the manifest
version, `new_install_prompt_erase` must be true, and the ESP32-S3 build must
contain exactly the four safe offsets. Drafts and invalid tags stay invisible.
If GitHub is unavailable, or no releases exist yet, the current Pages manifest
remains available as an Unreleased fallback.

To publish a version:

1. Set `FW_VERSION` in `TamaPoke.ino` and the README firmware badge to the same
   version.
2. Run `bash tools/build_web.sh`; this compiles the exact firmware, updates the
   manifest version and cache hashes, and checks that NVS is outside the write
   ranges.
3. Run `python3 tools/check_release.py vX.Y`. It also rejects stale firmware
   hashes or any disagreement among the tag, source, README, and manifest.
4. Commit and push those files, then create and push the tag:

   ```bash
   git tag -a vX.Y -m "TamaPoke vX.Y"
   git push origin main vX.Y
   ```

The `publish-release.yml` action validates the tag again, creates the GitHub
Release, and generates its changelog from commits and pull requests. The Pages
selector discovers it automatically; no HTML or catalogue edit is needed for
later releases.

## Installed pack identity

`paks.json` carries the CRC32 of each complete TPAK. The browser verifies that
download before sending any file, then uses three firmware commands:

```text
PACKS                         # PACK <region-index> <crc|legacy|missing>
PACK BEGIN <region-index>     # remove the old marker before changing files
PACK COMMIT <index> <crc32>   # write it only after all PUTs and probes pass
```

An interrupted upload therefore cannot continue to claim that the old version
is current. `PUT` also checks the SD write length and invalidates the relevant
marker itself, so `tools/send_sd.py` and manual uploads cannot leave a false
Current status behind. Cards populated before this protocol are detected by the
existing start/middle/end probes and shown as Installed / unversioned once;
reinstalling writes the exact version marker.

Every regional bundle includes the same shared `thumbs.bin`. During a managed
`PACK BEGIN` transaction, replacing that file invalidates only the region being
installed; a standalone custom upload still invalidates all markers. The page
also remembers exact hashes it verified during the current session and repairs
those markers after a successful batch, which preserves correct status when
installing several regions through older v3.16 firmware.

## Hosting the sprites

**The `.pak` files ARE committed, and that is deliberate.** They have to be
served **same-origin** from GitHub Pages, because **GitHub release assets send
no CORS headers at all** — a browser `fetch()` of one is blocked, however much
nicer it would be to keep 140 MB out of the repo. Verified with an `Origin`
header: the asset returns `200` and no `access-control-allow-origin`, while
Pages sends `access-control-allow-origin: *`.

This page said the opposite for a long time — "gitignored", "not committed",
"serves Access-Control-Allow-Origin" — while `.gitignore` carried the real
reason and the files were tracked. Acting on this file rather than on the code
untracks them and silently breaks every download button. It is written down
here now so the next person does not have to find out the same way.

```bash
bash tools/build_web.sh   # rebuilds the firmware, the manifest and every .pak
git add web/sprites-*.pak # yes, really
```

The page fetches the packs from the same directory as `index.html`. Same-origin
is the path that works in a browser and keeps CRC metadata beside the exact
bytes it describes.

**Why one file per region and not one big one:** all four together come to about
140 MB, and GitHub's hard per-file limit is 100 MB — a single bundle would be
uncommittable. Splitting also means most people can take Kanto (~40 MB) and
stop, and add a region later without re-sending what is already on the card.
A region whose `.pak` is not on the card shows as locked in the Pokedex chooser
and is kept out of the egg pool, so a partial install is a supported state
rather than a broken one.

All sprites are from PMD SpriteCollab, CC BY-NC (non-commercial sharing with
attribution is allowed); see [`../CREDITS.md`](../CREDITS.md).

## Deploy (GitHub Pages)

1. Repo settings → Pages → serve from `main`, folder `/web` (or move `web/` to
   `docs/`). Pages gives HTTPS automatically.
2. URL ends up at `https://<user>.github.io/<repo>/`.

> **Pages on private repos** needs GitHub Pro/Team. If you make the repo
> **public** to use Pages for free, decide about the sprites first (see above and
> CREDITS).

## Limitations

- Desktop **Chrome/Edge** only (Web Serial isn't in Firefox/Safari).
- Firmware from before the `PACKS` protocol can still receive packs and use save
   backup/restore, but the page cannot identify exact installed pack versions.
   Flash the current Standard edition once to enable comparison.
