// Tests for web/packs.js -- the sprite-pack bundle and the protocol that puts a
// pack on the card.
//
//   node tools/check_packs.mjs
//
// The upload runs against a FAKE BOARD modelled on sdmon.cpp: it acks each block,
// keeps what it was sent, and can refuse a file, go quiet mid-file, lack SUM, or
// store the wrong bytes the way a failing card does. The CRC is pinned to its
// published check value and the parser to a pack that actually ships, so neither
// is tested against this file's own idea of the format.
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import {
  crc32, parsePak, verifyPak, packIsCurrent, parseSum, probeSum, installFile,
  PUT_BLOCK, PUT_WINDOW, PUT_ATTEMPTS, RETRY_AFTER_TIMEOUT_MS, RETRY_AFTER_ERR_MS,
} from '../web/packs.js';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');

let bad = 0;
function ck(ok, what) {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${what}`);
  if (!ok) bad++;
}
async function ckThrows(fn, needle, what) {
  let message = null;
  try { await fn(); } catch (error) { message = error.message; }
  const ok = message !== null && message.toLowerCase().includes(needle.toLowerCase());
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${what}`);
  if (!ok) {
    console.log(`      wanted a reason mentioning "${needle}", got ${message === null ? 'no throw' : `"${message}"`}`);
    bad++;
  }
}

// ---- the checksum, against the published check value
ck(crc32(new TextEncoder().encode('123456789')) === 'cbf43926',
   'CRC-32 matches the published check value for "123456789"');
ck(crc32(new Uint8Array(0)) === '00000000', 'and an empty input is zero');

// ---- the bundle
function makePak(files) {
  const enc = new TextEncoder();
  const names = files.map((file) => enc.encode(file.name));
  let size = 6;
  files.forEach((file, i) => { size += 1 + names[i].length + 4 + file.data.length; });
  const out = new Uint8Array(size);
  const view = new DataView(out.buffer);
  out.set(enc.encode('TPAK'), 0);
  view.setUint16(4, files.length, true);
  let at = 6;
  files.forEach((file, i) => {
    out[at++] = names[i].length;
    out.set(names[i], at);
    at += names[i].length;
    view.setUint32(at, file.data.length, true);
    at += 4;
  });
  for (const file of files) { out.set(file.data, at); at += file.data.length; }
  return out.buffer;
}
const bytes = (n, seed) => Uint8Array.from({ length: n }, (_, i) => (i * 31 + seed) & 0xff);

{
  const files = [{ name: 'mons/p001.bin', data: bytes(5000, 1) }, { name: 'mons/thumbs.bin', data: bytes(9, 2) }];
  const items = parsePak(makePak(files));
  ck(items.length === 2 && items[0].name === 'mons/p001.bin' && items[1].name === 'mons/thumbs.bin',
     'a bundle reads back its names in order');
  ck(crc32(items[0].data) === crc32(files[0].data) && crc32(items[1].data) === crc32(files[1].data),
     'and its files byte for byte');
  const whole = new Uint8Array(makePak(files));
  await ckThrows(() => parsePak(whole.slice(0, whole.length - 1).buffer), 'truncated', 'a short bundle is refused');
  const longer = new Uint8Array(whole.length + 1);
  longer.set(whole);
  await ckThrows(() => parsePak(longer.buffer), 'trailing', 'and so is one with bytes after the last file');
  await ckThrows(() => verifyPak({ bytes: whole.length + 1, crc32: crc32(whole) }, whole.buffer), 'expected',
                 'a download of the wrong length is refused by its length');
  await ckThrows(() => verifyPak({ bytes: whole.length, crc32: '00000000' }, whole.buffer), 'does not match',
                 'and one with the wrong bytes by its checksum');
}

// A pack that actually ships, against the CRC and count paks.json publishes: the
// Python writer, the index and this reader have to agree, not just this file.
{
  const catalogue = JSON.parse(readFileSync(join(root, 'web/paks.json'), 'utf8')).regions;
  const [region, meta] = Object.entries(catalogue).sort((a, b) => a[1].bytes - b[1].bytes)[0];
  const blob = readFileSync(join(root, `web/sprites-${region}.pak`));
  const buffer = blob.buffer.slice(blob.byteOffset, blob.byteOffset + blob.byteLength);
  let ok = true;
  try { verifyPak(meta, buffer); } catch { ok = false; }
  ck(ok, `the committed ${region} pack matches paks.json's CRC and length`);
  ck(parsePak(buffer).length === meta.sprites, `and holds the ${meta.sprites} files paks.json says`);
}

// ---- which marker counts as current
{
  const meta = { crc32: 'AABBCCDD', equivalent: ['11223344'] };
  ck(packIsCurrent(meta, 'aabbccdd'), 'the published CRC is current, whatever its case');
  ck(packIsCurrent(meta, '11223344'), 'so is a recorded equivalent');
  ck(!packIsCurrent(meta, '55667788') && !packIsCurrent(meta, 'legacy'), 'and nothing else is');
}

// ---- SUM replies
ck(JSON.stringify(parseSum(['cargado x', 'SUM 0A0B0C0D 42'])) === '{"crc":"0a0b0c0d","size":42}',
   'SUM is read past other console output');
ck(parseSum(['DONE']) === null, 'and a reply without one is no answer');

// ---- the fake board
function concat(chunks) {
  const out = new Uint8Array(chunks.reduce((n, c) => n + c.length, 0));
  let at = 0;
  for (const c of chunks) { out.set(c, at); at += c.length; }
  return out;
}

function fakeBoard({ sums = true, refuseOnce = [], stallOnce = [], silent = false, corrupt = null } = {}) {
  const board = {
    card: new Map(), queue: [], puts: [], pauses: [], logs: [],
    maxInflight: 0, overflow: false,
  };
  const refuse = new Set(refuseOnce);
  const stall = new Set(stallOnce);
  let put = null;
  board.io = {
    async writeLine(line) {
      if (put?.stalled) return;                     // still reading the old file: this is data
      const match = /^PUT (\S+) (\d+)$/.exec(line);
      if (!match || silent) return;
      board.puts.push(match[1]);
      if (refuse.delete(match[1])) { board.queue.push('ERR'); return; }
      put = { name: match[1], size: Number(match[2]), chunks: [], received: 0, inflight: 0,
              stalled: stall.delete(match[1]) };
      board.queue.push('OK');
    },
    async writeBytes(chunk) {
      if (!put) throw new Error('fake board: data arrived outside a PUT');
      put.chunks.push(chunk);
      put.received += chunk.length;
      put.inflight++;
      board.maxInflight = Math.max(board.maxInflight, put.inflight);
      if (put.inflight > PUT_WINDOW) board.overflow = true;   // past the 8 KB the board can hold
    },
    async waitForAny(tokens) {
      while (board.queue.length) {
        const line = board.queue.shift();
        if (tokens.includes(line)) return line;
      }
      if (!put || put.stalled) return null;
      if (tokens.includes('#') && put.inflight > 0) { put.inflight--; return '#'; }
      if (tokens.includes('DONE') && put.received === put.size && put.inflight === 0) {
        const data = concat(put.chunks);
        board.card.set(put.name, corrupt ? corrupt(data) : data);
        put = null;
        return 'DONE';
      }
      return null;
    },
    async command(line) {
      const match = /^SUM \/?(\S+)$/.exec(line);
      if (!match || !sums || put?.stalled) return { outcome: null, lines: [] };
      const stored = board.card.get(match[1]);
      if (!stored) return { outcome: 'ERR', lines: [] };
      return { outcome: 'DONE', lines: [`SUM ${crc32(stored)} ${stored.length}`] };
    },
    async pause(ms) {
      board.pauses.push(ms);
      // What sdmon.cpp does with a file that stops arriving: it keeps reading for up
      // to 10 s, then acks what it got and says ERR -- late, into the queue.
      if (put?.stalled && ms >= 10000) {
        board.queue.push('#', 'ERR');
        put = null;
      }
    },
    resetQueue() { board.queue = []; },
    log(message) { board.logs.push(message); },
  };
  return board;
}

const file = { name: 'mons/p025.bin', data: bytes(PUT_BLOCK * 7 + 100, 25) };

// The window is only safe while it fits the board's receive buffer, which drops
// what does not fit -- so the limit is read from the firmware, not restated here.
{
  const sketch = readFileSync(join(root, 'TamaPoke.ino'), 'utf8');
  const rx = Number(/Serial\.setRxBufferSize\((\d+)\)/.exec(sketch)?.[1]);
  ck(rx > 0 && PUT_WINDOW * PUT_BLOCK + 64 <= rx,
     `${PUT_WINDOW} blocks of ${PUT_BLOCK} bytes and a PUT line fit the board's ${rx}-byte receive buffer`);
}

{
  const board = fakeBoard({ sums: false });
  const seen = [];
  const result = await installFile(board.io, file, { sums: false, onProgress: (n) => seen.push(n) });
  ck(result === 'sent', 'a file is sent');
  ck(crc32(board.card.get(file.name)) === crc32(file.data), 'and the card holds exactly its bytes');
  ck(!board.overflow && board.maxInflight === PUT_WINDOW,
     `no more than ${PUT_WINDOW} blocks are ever in flight, and the window is actually used`);
  ck(seen.at(-1) === file.data.length && seen.every((n, i) => !i || n >= seen[i - 1]),
     'progress climbs to the whole file without going backwards');
}

{
  const board = fakeBoard();
  ck(await probeSum(board.io), 'firmware with SUM is recognised by its answer to the probe');
  ck(!await probeSum(fakeBoard({ sums: false }).io), 'and firmware without it by its silence');
}

{
  const board = fakeBoard();
  board.card.set(file.name, file.data.slice());
  const result = await installFile(board.io, file, { sums: true });
  ck(result === 'skipped' && board.puts.length === 0,
     'a file the card already holds is skipped, which is what lets an install resume');
}

{
  const board = fakeBoard();
  board.card.set(file.name, bytes(10, 99));
  const result = await installFile(board.io, file, { sums: true });
  ck(result === 'sent' && board.puts.length === 1, 'a file that differs is sent');
  ck(crc32(board.card.get(file.name)) === crc32(file.data), 'and read back matching');
}

{
  // A failing or counterfeit card: every write "succeeds" and none of it is kept.
  const board = fakeBoard({ corrupt: (data) => { const out = data.slice(); out[out.length >> 1] ^= 0xff; return out; } });
  await ckThrows(() => installFile(board.io, file, { sums: true }), 'did not read back',
                 'a card that does not keep what it is given fails the file, and says why');
  ck(board.puts.length === PUT_ATTEMPTS, `after ${PUT_ATTEMPTS} attempts, not one`);
  ck(board.pauses.length === 0, 'without waiting out a timeout: the board answered every time');
}

{
  const board = fakeBoard({ refuseOnce: [file.name] });
  const result = await installFile(board.io, file, { sums: true });
  ck(result === 'sent' && board.puts.length === 2, 'a refused file is retried');
  ck(board.pauses.length === 1 && board.pauses[0] === RETRY_AFTER_ERR_MS,
     'after the short pause, since the board is back at its prompt');
}

{
  // The board goes quiet mid-file. It is still reading that file, so a retry has to
  // outwait it; and it then says ERR late, which must not be taken for the retry's answer.
  const board = fakeBoard({ stallOnce: [file.name] });
  const result = await installFile(board.io, file, { sums: true });
  ck(result === 'sent' && board.puts.length === 2, 'a file that stalls is retried and arrives');
  ck(board.pauses[0] === RETRY_AFTER_TIMEOUT_MS && RETRY_AFTER_TIMEOUT_MS >= 10000,
     'after outwaiting the board, which is still reading the stalled file');
  ck(crc32(board.card.get(file.name)) === crc32(file.data), 'and what lands is the real file');
}

{
  const board = fakeBoard({ silent: true });
  await ckThrows(() => installFile(board.io, file, { sums: false }), 'stopped answering',
                 'a board that never answers fails the file with a reason');
}

console.log(bad ? `\n${bad} FAILURE(S)` : '\nall good');
process.exit(bad ? 1 : 0);
