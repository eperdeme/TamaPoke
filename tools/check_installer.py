#!/usr/bin/env python3
"""Proves the web installer cannot erase a player's pet.

The save lives in the NVS partition, and TWO separate things were destroying it.
Both are checked here because fixing either one alone still loses the save.

1. WHAT IS WRITTEN. web/firmware/tamapoke.bin was a single image starting at
   offset 0, and esptool's merge-bin pads the gaps, so it wrote 0xFF straight
   over NVS at 0x9000. The manifest must ship the four parts at their own
   offsets instead, leaving 0x9000..0xE000 alone.

2. WHETHER THE CHIP IS ERASED FIRST, which no arrangement of parts can survive.
   In esp-web-tools' no-Improv path -- ours, this firmware speaks no Improv --
   the Install button is:

       new_install_prompt_erase ? state = "ASK_ERASE" : _startInstall(true)

   The name is the exact opposite of what it does. FALSE means "do not ask,
   just erase", and _startInstall(true) calls eraseFlash(), a whole-chip erase.
   TRUE shows a screen with an "Erase device" checkbox that starts UNCHECKED.
   It was set false deliberately, reading the name at face value, and that
   destroyed two real saves.

The partition table is read out of the build itself rather than hardcoded, so a
partition scheme change moves the check with it.

    python3 tools/check_installer.py                    # checks web/manifest.json
    python3 tools/check_installer.py web/beta/manifest.json

Run by build_web.sh on every build, so neither cause can come back.
"""
import hashlib
import json
import os
import struct
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "web")


def partitions(path):
    """The partition table as (label, offset, size), read from the .bin."""
    out = []
    with open(path, "rb") as f:
        blob = f.read()
    for i in range(0, len(blob), 32):
        e = blob[i:i + 32]
        if len(e) < 32 or e[:2] != b"\xAA\x50":
            break
        off, size = struct.unpack("<II", e[4:12])
        label = e[12:28].rstrip(b"\x00").decode("ascii", "replace")
        out.append((label, off, size))
    return out


def check_flash_trigger():
    """THE FLASH BUTTON MUST STILL BE CLICKABLE BY A HUMAN, AND ONLY BY A HUMAN.

    esp-web-tools binds its handler to a <slot name="activate"> created inside its
    own shadow root:

        const o = document.createElement("slot");
        o.addEventListener("click", async t => { t.preventDefault(); e(this) });
        o.name = "activate";

    Two consequences, and v3.22 shipped both:

      * a click dispatched on the HOST element never reaches that listener, because
        it does not propagate into the shadow tree. `flashButton.click()` was
        therefore a no-op -- the backup ran, the log claimed the port had been
        handed over, and no installer ever appeared. Silent, with no error.
      * that handler calls navigator.serial.requestPort(), which needs TRANSIENT
        USER ACTIVATION. So even aimed at the slotted button it cannot be driven
        from code after an await: the activation is spent and expired.

    So this refuses a build that loses the slotted button, and refuses one that
    tries to synthesise the click again. Neither failure is visible in a
    screenshot, and neither raises anything in the page.
    """
    bad = 0
    with open(os.path.join(ROOT, "web", "index.html")) as fh:
        html = fh.read()
    with open(os.path.join(ROOT, "web", "installer.js"), "rb") as fh:
        js_bytes = fh.read()
    js = js_bytes.decode("utf-8")
    # COMMENTS ARE STRIPPED FIRST. The note explaining this very regression names
    # the call it is warning about, and a lint that reads prose flagged the
    # explanation as the bug -- which would have meant deleting the comment to
    # make the check pass. Scan code only.
    js = re.sub(r'/\*.*?\*/', '', js, flags=re.S)
    js = re.sub(r'(?m)^\s*//.*$', '', js)

    if not re.search(r'<esp-web-install-button\b', html):
        print("\nFAIL: web/index.html has no <esp-web-install-button>")
        return 1
    cache_key = re.search(r'installer\.js\?v=([0-9a-f]{16})', html)
    expected_key = hashlib.sha256(js_bytes).hexdigest()[:16]
    if not cache_key or cache_key.group(1) != expected_key:
        actual_key = cache_key.group(1) if cache_key else "missing"
        print("\nFAIL: web/index.html requests installer.js cache key %s, but "
              "the current script is %s. Browsers can keep running the old "
              "installer code." % (actual_key, expected_key))
        bad += 1
    # The slotted button, inside the element rather than merely somewhere on the page.
    block = re.search(r'<esp-web-install-button\b.*?</esp-web-install-button>', html, re.S)
    if not block or 'slot="activate"' not in block.group(0):
        print("\nFAIL: the install button has no child with slot=\"activate\". "
              "esp-web-tools listens on a <slot name=\"activate\"> in its shadow "
              "root, so without it NOTHING can start a flash.")
        bad += 1

    if 'id="backup-save"' not in html or 'Back up save' not in html:
        print("\nFAIL: web/index.html has no separate Back up save action.")
        bad += 1
    if not block or 'Install firmware' not in block.group(0):
        print("\nFAIL: the slotted esp-web-tools control is not labelled Install firmware.")
        bad += 1

    # Any synthesised click is invalid here: the host misses the shadow-root
    # listener, while the slotted button reaches it without user activation.
    for pattern, why in (
        (r'byId\([\'"]flash-button[\'"]\)\s*\.click\(\)',
         "clicks the install button's HOST, which never reaches its shadow-root listener"),
        (r'flashButton\s*\.click\(\)',
         "clicks the install button's HOST, which never reaches its shadow-root listener"),
        (r'activate\s*\.click\(\)',
         "synthesises an install click after transient user activation has expired"),
    ):
        if re.search(pattern, js):
            print("\nFAIL: web/installer.js %s.\n"
                  "      The flash must be left to a real user click -- "
                  "requestPort() needs transient activation." % why)
            bad += 1
    return bad

def main():
    manifest_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(WEB, "manifest.json")
    manifest_path = os.path.abspath(manifest_path)
    manifest_dir = os.path.dirname(manifest_path)
    manifest = json.load(open(manifest_path))

    parts = []
    for build in manifest.get("builds", []):
        for p in build.get("parts", []):
            # The manifest cache-busts each part with "?v=<hash>". That is a URL,
            # not a filename, so it has to come off before touching the disk --
            # otherwise this guard reports MISSING for a file that is right there
            # and the build fails for the wrong reason.
            rel = p["path"].split("?", 1)[0]
            path = os.path.join(manifest_dir, rel)
            if not os.path.exists(path):
                print("MISSING: %s (named by the manifest)" % rel)
                return 1
            parts.append((rel, int(p["offset"]), os.path.getsize(path)))

    if not parts:
        print("the manifest lists no parts at all")
        return 1

    # The partition table is one of the parts we ship, so the layout is read
    # from the same bytes the board will be given.
    table = None
    for path, off, _ in parts:
        if off == 0x8000:
            table = partitions(os.path.join(manifest_dir, path))
    if table is None:
        # a single merged image carries the table inside it at 0x8000
        for path, off, size in parts:
            if off == 0 and size > 0x9000:
                blob = open(os.path.join(manifest_dir, path), "rb").read()
                tmp = os.path.join(manifest_dir, ".ptable.tmp")
                open(tmp, "wb").write(blob[0x8000:0x9000])
                table = partitions(tmp)
                os.remove(tmp)
    if not table:
        print("could not find a partition table in anything the manifest ships")
        return 1

    keep = [(l, o, s) for (l, o, s) in table if l in ("nvs", "ffat")]
    print("what the installer writes:")
    for path, off, size in sorted(parts, key=lambda p: p[1]):
        print("  0x%06X..0x%06X  %s" % (off, off + size, path))
    print("what must survive it:")
    for label, off, size in keep:
        print("  0x%06X..0x%06X  %s" % (off, off + size, label))

    bad = 0
    for label, poff, psize in keep:
        for path, off, size in parts:
            if off < poff + psize and poff < off + size:
                print("\nFAIL: %s (0x%06X..0x%06X) is written over by %s "
                      "(0x%06X..0x%06X)" % (label, poff, poff + psize, path,
                                            off, off + size))
                if label == "nvs":
                    print("      That is the save. Every update would destroy "
                          "the player's pet.")
                bad += 1

    # The name lies. In esp-web-tools' no-Improv path (ours), the Install button
    # is `new_install_prompt_erase ? ASK_ERASE : _startInstall(true)` -- so
    # FALSE means erase the whole chip without asking, and TRUE means show a
    # checkbox that defaults to not erasing. Setting it false to "stop it
    # erasing" is what destroyed two real saves.
    if not manifest.get("new_install_prompt_erase"):
        print("\nFAIL: new_install_prompt_erase is false, which in the "
              "no-Improv path means a SILENT full chip erase -- it must be "
              "true, which offers an unchecked 'Erase device' box instead")
        bad += 1

    bad += check_flash_trigger()
    print("\n%s" % ("FAILURES" if bad else "the save is out of the blast radius"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
