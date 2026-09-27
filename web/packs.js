// Sprite-pack logic for the installer: the TPAK bundle, its CRC-32, and the
// protocol that puts one file on the card.
//
// Like savefile.js, and for the same reason, nothing here may reach for `window`,
// `document` or `navigator`: node imports it, and tools/check_packs.mjs drives the
// upload against a fake board. The serial I/O arrives as an argument.

// The board acks each 2 KB block. Three in flight hide the round trip behind the
// SD write, and that is safe on every firmware: all have an 8 KB receive buffer,
// and the board's USB driver DROPS what does not fit rather than pushing back.
export const PUT_BLOCK = 2048;
export const PUT_WINDOW = 3;
export const PUT_ATTEMPTS = 3;
// A board whose file stops arriving keeps reading it for up to 10 s (two 5 s
// reads) before giving it up, and a retry sent sooner is written INTO that file.
// After ERR it is already back at its prompt; only its 1 s line timeout remains.
export const RETRY_AFTER_TIMEOUT_MS = 11000;
export const RETRY_AFTER_ERR_MS = 1200;
export const SUM_TIMEOUT_MS = 15000;
export const SUM_PROBE_TIMEOUT_MS = 3000;

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let index = 0; index < 256; index++) {
    let value = index;
    for (let bit = 0; bit < 8; bit++) value = (value >>> 1) ^ (0xEDB88320 & -(value & 1));
    table[index] = value >>> 0;
  }
  return table;
})();

// CRC-32/ISO-HDLC (zlib's), as hex: what paks.json records and SUM reports.
export function crc32(bytes) {
  let crc = 0xFFFFFFFF;
  for (let i = 0; i < bytes.length; i++) crc = CRC_TABLE[(crc ^ bytes[i]) & 0xFF] ^ (crc >>> 8);
  return ((crc ^ 0xFFFFFFFF) >>> 0).toString(16).padStart(8, '0');
}

export function parsePak(buffer) {
  if (buffer.byteLength < 6) throw new Error('bundle is too short');
  const view = new DataView(buffer);
  const text = new TextDecoder();
  if (text.decode(new Uint8Array(buffer, 0, 4)) !== 'TPAK') throw new Error('invalid bundle');
  const count = view.getUint16(4, true);
  let offset = 6;
  const items = [];
  for (let index = 0; index < count; index++) {
    if (offset + 1 > buffer.byteLength) throw new Error('truncated bundle index');
    const nameLength = view.getUint8(offset++);
    if (!nameLength || offset + nameLength + 4 > buffer.byteLength) throw new Error('invalid bundle index');
    const name = text.decode(new Uint8Array(buffer, offset, nameLength));
    offset += nameLength;
    const size = view.getUint32(offset, true);
    offset += 4;
    items.push({ name, size });
  }
  let payload = offset;
  for (const item of items) {
    if (payload + item.size > buffer.byteLength) throw new Error('truncated bundle payload');
    item.data = new Uint8Array(buffer, payload, item.size);
    payload += item.size;
  }
  if (payload !== buffer.byteLength) throw new Error('bundle contains trailing data');
  return items;
}

export function verifyPak(meta, buffer) {
  if (buffer.byteLength !== meta.bytes) throw new Error(`expected ${meta.bytes} bytes, received ${buffer.byteLength}`);
  const checksum = crc32(new Uint8Array(buffer));
  if (checksum !== meta.crc32.toLowerCase()) throw new Error(`checksum ${checksum} does not match ${meta.crc32}`);
}

// `equivalent` lists older builds of a pack whose sprites are byte-identical to
// this one (tools/pack_bundle.py proves it). They differ only in the shared
// thumbs.bin, which every pack carries, so re-sending one changes nothing it draws.
export function packIsCurrent(meta, installed) {
  return installed === meta.crc32.toLowerCase()
    || (meta.equivalent || []).some((crc) => crc.toLowerCase() === installed);
}

// "SUM <crc32> <bytes>", which firmware with the command prints before DONE.
export function parseSum(lines) {
  for (const line of lines || []) {
    const match = /^SUM ([0-9a-fA-F]{8}) (\d+)$/.exec(line);
    if (match) return { crc: match[1].toLowerCase(), size: Number(match[2]) };
  }
  return null;
}

// Asks about a file that cannot exist: firmware with SUM answers ERR at once and
// older firmware says nothing, so no real file ever has to wait out a timeout.
//
// `io` is { writeLine, writeBytes, waitForAny, command, pause } and may provide
// { resetQueue, log }; command(line, ms) resolves { outcome: 'DONE'|'ERR'|null, lines }.
export async function probeSum(io) {
  const { outcome } = await io.command('SUM /mons/.sum-probe', SUM_PROBE_TIMEOUT_MS);
  return outcome !== null;
}

// true when the card holds exactly `want`, false when it does not, null when the
// board did not answer at all.
async function storedMatches(io, name, want) {
  const { outcome, lines } = await io.command(`SUM ${name}`, SUM_TIMEOUT_MS);
  if (outcome === null) return null;
  const sum = outcome === 'DONE' ? parseSum(lines) : null;
  return Boolean(sum && sum.crc === want.crc && sum.size === want.size);
}

// One PUT: 'ok', 'refused' when the board said ERR, or 'timeout' when it went
// quiet -- in which case it may still be reading this file.
export async function putFile(io, name, data, onProgress) {
  await io.writeLine(`PUT ${name} ${data.length}`);
  const opened = await io.waitForAny(['OK', 'ERR']);
  if (opened !== 'OK') return opened === 'ERR' ? 'refused' : 'timeout';
  let unacked = 0;
  let acked = 0;
  const ack = async () => {
    const reply = await io.waitForAny(['#', 'ERR']);
    if (reply !== '#') return reply === 'ERR' ? 'refused' : 'timeout';
    unacked--;
    acked = Math.min(data.length, acked + PUT_BLOCK);
    onProgress?.(acked);
    return null;
  };
  for (let offset = 0; offset < data.length; offset += PUT_BLOCK) {
    if (unacked === PUT_WINDOW) {
      const failed = await ack();
      if (failed) return failed;
    }
    await io.writeBytes(data.slice(offset, offset + PUT_BLOCK));
    unacked++;
  }
  while (unacked > 0) {
    const failed = await ack();
    if (failed) return failed;
  }
  const done = await io.waitForAny(['DONE', 'ERR'], 30000);
  if (done === 'DONE') return 'ok';
  return done === 'ERR' ? 'refused' : 'timeout';
}

// Puts one pack file on the card, and says 'sent' or 'skipped' -- or throws why not.
//
// With `sums` (firmware that has SUM) a file the card already holds byte for byte
// is skipped, which is what lets an interrupted install resume and an update send
// only what changed; and every file that IS sent is read back and compared, so
// "verified" describes the card rather than the download. A failing or counterfeit
// card accepts every write, and only reading it back can tell.
export async function installFile(io, item, { sums = false, onProgress } = {}) {
  const want = { crc: crc32(item.data), size: item.data.length };
  if (sums && await storedMatches(io, item.name, want) === true) {
    onProgress?.(item.data.length);
    return 'skipped';
  }
  let reason = '';
  for (let attempt = 1; attempt <= PUT_ATTEMPTS; attempt++) {
    onProgress?.(0);
    const result = await putFile(io, item.name, item.data, onProgress);
    let wait = result === 'timeout' ? RETRY_AFTER_TIMEOUT_MS : RETRY_AFTER_ERR_MS;
    if (result === 'ok') {
      const stored = sums ? await storedMatches(io, item.name, want) : true;
      if (stored === true) return 'sent';
      reason = stored === false
        ? `${item.name} did not read back as it was written: the card may be failing or counterfeit`
        : `the board stopped answering while ${item.name} was checked`;
      wait = stored === false ? 0 : RETRY_AFTER_TIMEOUT_MS;
    } else {
      reason = result === 'refused'
        ? `the board could not write ${item.name}`
        : `the board stopped answering while ${item.name} was sent`;
    }
    if (attempt < PUT_ATTEMPTS) {
      io.log?.(`Retrying ${item.name} (${attempt}/${PUT_ATTEMPTS - 1}): ${reason}.`);
      if (wait) await io.pause(wait);
      // After the pause, not before it: a late ERR from the abandoned attempt
      // would otherwise be read as the retry's answer.
      io.resetQueue?.();
    }
  }
  throw new Error(reason);
}
