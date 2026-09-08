// Save-file logic for the installer: parsing, verification and the checksum.
//
// It lives in its own file for ONE reason -- it has no browser dependencies, so
// `node` can import it and tools/check_savefile.mjs can test it. Everything that
// needs a DOM, a serial port or IndexedDB stays in installer.js. Nothing here
// may reach for `window`, `document` or `navigator`.
//
// The format is produced by save.cpp on the board:
//
//   # TamaPoke save, N bytes. Paste this whole block back.
//   IMPORT <hex>      one chunk, repeated
//   IMPORT            the commit line, exactly once, last
//
// and the decoded blob is:
//
//   off 0   'T','K','P','S'
//   off 4   version
//   off 5   u16  field count
//   off 7   0
//   off 8   the fields
//   end-2   u16  CRC-16/CCITT-FALSE over everything before it
//
// The same verifyBackup() runs on BOTH directions on purpose: the block we hand
// a player is checked by the identical function that checks the block they hand
// back. A backup is only worth having if it was verified at the moment it was
// taken -- the firmware validates on restore, but by then the save it was meant
// to protect may be long gone.

export const SAVE_MAGIC = 'TKPS';
export const SAVE_VERSION = 1;
export const SAVE_HEADER_BYTES = 8;
export const SAVE_CRC_BYTES = 2;

// CRC-16/CCITT-FALSE: init 0xFFFF, poly 0x1021, MSB first, no reflection and no
// final xor. Byte-for-byte the crc16() in save.cpp. Its standard check value --
// "123456789" gives 0x29B1 -- is asserted in the tests, so this is pinned
// against the published definition rather than against a copy of itself.
export function crc16(bytes) {
  let c = 0xffff;
  for (const b of bytes) {
    c ^= b << 8;
    for (let i = 0; i < 8; i++) c = (c & 0x8000) ? ((c << 1) ^ 0x1021) & 0xffff : (c << 1) & 0xffff;
  }
  return c & 0xffff;
}

export function hexToBytes(hex) {
  if (hex.length % 2) throw new Error('hex has an odd number of digits');
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < out.length; i++) {
    const byte = Number.parseInt(hex.slice(i * 2, i * 2 + 2), 16);
    if (Number.isNaN(byte)) throw new Error('hex contains a non-hex digit');
    out[i] = byte;
  }
  return out;
}

const HEADER_RE = /^#\s*TamaPoke save,\s*(\d+)\s*bytes/i;

// Splits the text into console commands and pulls out the byte count the board
// declared. Format only -- verifyBackup() is what decides whether it is sound.
export function parseBackup(text) {
  const lines = String(text).split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
  const commands = [];
  let declared = null;
  let sawCommit = false;
  for (const line of lines) {
    if (line.startsWith('#')) {
      const match = HEADER_RE.exec(line);
      if (match) declared = Number.parseInt(match[1], 10);
      continue;
    }
    if (sawCommit) throw new Error('there is content after the final IMPORT line');
    if (line === 'IMPORT') {
      sawCommit = true;
      commands.push(line);
      continue;
    }
    if (!/^IMPORT [0-9a-fA-F]+$/.test(line)) {
      throw new Error(`unrecognised backup line: ${line.slice(0, 32)}`);
    }
    const hex = line.slice(7);
    if (hex.length % 2) throw new Error('a data line has an odd number of hex digits');
    commands.push(line);
  }
  if (!sawCommit) throw new Error('backup is missing its final IMPORT commit line');
  if (commands.length < 2) throw new Error('backup has no data, only a commit line');
  const hex = commands.slice(0, -1).map((line) => line.slice(7)).join('');
  return { commands, declared, blob: hexToBytes(hex) };
}

// Everything parseBackup() checks, plus the parts that decide whether the bytes
// are actually a sound save: the declared length, the magic, the version and the
// checksum. Throws with a specific reason; returns what a caller needs.
export function verifyBackup(text) {
  const parsed = parseBackup(text);
  const { blob, declared } = parsed;

  // The board says how many bytes it produced. Comparing against it is what
  // catches a TRUNCATED capture -- a serial read that stopped early still ends
  // with a plausible commit line, and without this the result was accepted and
  // handed over as a backup. A quietly partial backup is worse than none.
  if (declared !== null && blob.length !== declared) {
    throw new Error(`backup is ${blob.length} bytes but declares ${declared}`);
  }
  if (blob.length < SAVE_HEADER_BYTES + SAVE_CRC_BYTES) {
    throw new Error(`backup is too short to be a save (${blob.length} bytes)`);
  }
  const magic = String.fromCharCode(...blob.slice(0, 4));
  if (magic !== SAVE_MAGIC) throw new Error('this is not a TamaPoke save');
  if (blob[4] !== SAVE_VERSION) {
    throw new Error(`save format version ${blob[4]}, this page understands ${SAVE_VERSION}`);
  }
  const body = blob.subarray(0, blob.length - SAVE_CRC_BYTES);
  const want = blob[blob.length - 2] | (blob[blob.length - 1] << 8);
  const got = crc16(body);
  if (got !== want) {
    throw new Error(`checksum mismatch: the data says ${hex4(want)}, the bytes give ${hex4(got)}`);
  }
  return { ...parsed, fields: blob[5] | (blob[6] << 8) };
}

function hex4(v) {
  return `0x${v.toString(16).toUpperCase().padStart(4, '0')}`;
}

// Reads the few fields worth showing a player, so a stored backup can be
// labelled with something recognisable instead of only a timestamp. Walks the
// key-driven format save.cpp writes: klen, key, kind, u16 length, value.
//
// Deliberately TOLERANT: it is only used for a label, so anything it cannot make
// sense of comes back undefined rather than throwing and losing the backup a
// player is trying to keep.
export function describeBackup(blob) {
  const out = {};
  try {
    let at = SAVE_HEADER_BYTES;
    const end = blob.length - SAVE_CRC_BYTES;
    while (at + 4 <= end) {
      const klen = blob[at];
      if (!klen || at + 1 + klen + 3 > end) break;
      const key = String.fromCharCode(...blob.subarray(at + 1, at + 1 + klen));
      at += 1 + klen + 1;                       // key, then the kind byte
      const len = blob[at] | (blob[at + 1] << 8);
      at += 2;
      if (at + len > end) break;
      const value = blob.subarray(at, at + len);
      at += len;
      if (key === 'tnam' && len) out.trainer = trimText(value);
      else if (key === 'dexn' && len >= 2) out.dex = (value[0] | (value[1] << 8)) << 16 >> 16;
      else if (key === 'age' && len >= 4) {
        out.ageMinutes = value[0] | (value[1] << 8) | (value[2] << 16) | (value[3] << 24);
      }
    }
  } catch {
    // a label is never worth failing a backup over
  }
  return out;
}

function trimText(bytes) {
  let text = '';
  for (const b of bytes) {
    if (!b) break;
    text += String.fromCharCode(b);
  }
  return text.trim();
}
