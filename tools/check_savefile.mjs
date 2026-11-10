// Tests for web/savefile.js -- the browser half of the save backup.
//
//   node tools/check_savefile.mjs
//
// This is the only JavaScript in the project with tests, and it exists because
// the backup path had none at all: the FIRMWARE side of EXPORT/IMPORT is covered
// by console_test and save_test, while the code in the browser that captures,
// verifies and restores that data was 748 untested lines. A backup is the one
// feature where a silent bug is unrecoverable by definition.
//
// Two rules shape what is asserted:
//
//   * The checksum is pinned to the PUBLISHED check value for CRC-16/CCITT-FALSE
//     ("123456789" -> 0x29B1), not to a second copy of the algorithm. Comparing
//     an implementation against a re-implementation only proves they were typed
//     the same way.
//
//   * The golden fixture in tools/fixtures/ was PRODUCED BY THE REAL FIRMWARE
//     (the emulator's EXPORT) and the real firmware accepts it back. So parsing
//     it here tests the JS against C++ output rather than against this file's
//     idea of the format -- see CLAUDE.md § "A test that proves the
//     transcription rather than the firmware".
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import {
  crc16, hexToBytes, parseBackup, verifyBackup, describeBackup, sendBackup,
  saveSummary, backupFileName, SAVE_CRC_BYTES, SAVE_TRANSFER_MAX,
} from '../web/savefile.js';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..');
const golden = readFileSync(join(root, 'tools/fixtures/golden.tpsave'), 'utf8');

let bad = 0;
function ck(ok, what) {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${what}`);
  if (!ok) bad++;
}
// Asserts a call throws, and that the reason MENTIONS what went wrong -- a test
// that only checks "it threw" passes for a throw from the wrong line.
function ckThrows(fn, needle, what) {
  let message = null;
  try { fn(); } catch (error) { message = error.message; }
  const ok = message !== null && message.toLowerCase().includes(needle.toLowerCase());
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${what}`);
  if (!ok) {
    console.log(`      wanted a reason mentioning "${needle}", got ${message === null ? 'no throw' : `"${message}"`}`);
    bad++;
  }
}

// ---- the checksum, against the published check value
ck(crc16(new TextEncoder().encode('123456789')) === 0x29b1,
   'CRC-16/CCITT-FALSE matches the published check value for "123456789"');
ck(crc16(new Uint8Array(0)) === 0xffff, 'and an empty input is the init value');

// ---- hex decoding
ck(hexToBytes('544B5053').join(',') === '84,75,80,83', 'hex decodes to the right bytes');
ckThrows(() => hexToBytes('ABC'), 'odd', 'odd-length hex is refused');
ckThrows(() => hexToBytes('ZZ'), 'non-hex', 'non-hex digits are refused');
ckThrows(() => hexToBytes('0Z'), 'non-hex', 'partially valid hex pairs are refused too');

// ---- the golden fixture, produced by the firmware itself
const parsed = verifyBackup(golden);
ck(parsed.blob.length === 2128, `the firmware's own export verifies (${parsed.blob.length} bytes)`);
ck(parsed.declared === parsed.blob.length,
   'and the declared byte count matches the hex that arrived');
ck(parsed.commands.at(-1) === 'IMPORT', 'the commit line is last');
ck(parsed.commands.filter((c) => c === 'IMPORT').length === 1, 'and appears exactly once');
ck(parsed.fields > 40, `the field count reads back sensibly (${parsed.fields})`);

const described = describeBackup(parsed.blob);
console.log(`      label from the blob: dex=${described.dex} age=${described.ageMinutes}`);
ck(described.dex === 6, 'the label reads the species the fixture was built with (CHARIZARD)');
ck(parsed.version === 1, 'the original firmware-produced v1 backup remains supported');
if (process.env.TAMA_EXPANDED_SAVE_FIXTURE) {
  const expanded = verifyBackup(readFileSync(process.env.TAMA_EXPANDED_SAVE_FIXTURE, 'utf8'));
  ck(expanded.version === 3 && expanded.blob.length > 135000,
     'the real console-emitted future-stride v3 backup verifies in the browser');
  let at = 8, found = 0, newest = null, newestDims = 0;
  const transfer = new DataView(expanded.blob.buffer, expanded.blob.byteOffset, expanded.blob.length);
  while (at < expanded.blob.length - 2) {
    const space = expanded.blob[at++], k = expanded.blob[at++];
    const key = Buffer.from(expanded.blob.subarray(at, at + k)).toString(); at += k + 1;
    const length = transfer.getUint32(at, true); at += 4;
    if (space === 1 && ['cbA', 'cbB'].includes(key)) {
      const value = new DataView(expanded.blob.buffer, expanded.blob.byteOffset + at, length);
      const dims = value.getUint16(6, true);
      ck(value.getUint16(dims + 4, true) === 1200 && value.getUint16(dims, true) === 56 &&
         length > 65535, `real ${key} individually exceeds 65535 bytes with all 1200 future-stride records`);
      if (!newest || value.getUint32(12, true) > newest.getUint32(12, true)) {
        newest = value;
        newestDims = dims;
      }
      found++;
    }
    at += length;
  }
  ck(found === 2, 'both real firmware collection slots are present, not silently omitted');
  const last = newest && newestDims + 6
    + newest.getUint16(newestDims, true) * (newest.getUint16(newestDims + 2, true) + 1199);
  ck(newest && newest.getInt16(last, true) === 1025 && newest.getUint16(last + 2, true) === 88,
     'the real authoritative collection contains the correct dex and level in slot 1199');
}

// Historical transfers and future-stride snapshots, independently of the small
// v1 fixture. The real console fixture above is the cross-language evidence.
function backupText(blob, reseal = true) {
  if (reseal) {
    const c = crc16(blob.subarray(0, blob.length - 2));
    blob[blob.length - 2] = c & 255; blob[blob.length - 1] = c >> 8;
  }
  const hex = Buffer.from(blob).toString('hex');
  return `# TamaPoke save, ${blob.length} bytes.\n`
    + hex.match(/.{1,192}/g).map((chunk) => `IMPORT ${chunk}`).join('\n') + '\nIMPORT\n';
}
function backupBlob(entries, version = 3) {
  const bytes = [84, 75, 80, 83, version, entries.length & 255, entries.length >> 8, 0];
  for (const { space, key, kind, value } of entries) {
    if (version < 3 && value.length > 65535) throw new Error('test fixture exceeds a legacy field length');
    if (version >= 2) bytes.push(space);
    bytes.push(key.length, ...Buffer.from(key), kind);
    for (let b = 0; b < (version >= 3 ? 4 : 2); b++) bytes.push((value.length >>> (8 * b)) & 255);
    for (const b of value) bytes.push(b);
  }
  return new Uint8Array([...bytes, 0, 0]);
}
const v2Backup = (entries) => backupBlob(entries, 2);
function collectionRecord({ wide = false, stride = 48, headerSize = 16, tail = 0 } = {}) {
  const dims = wide ? headerSize : 12;
  const offset = dims + 6;
  const value = new Uint8Array(offset + stride * (6 + 1200) + tail + 2);
  const view = new DataView(value.buffer);
  value.set(Buffer.from(wide ? 'KBC3' : 'KBC2'));
  view.setUint16(4, 1, true);
  if (wide) {
    view.setUint16(6, headerSize, true);
    view.setUint32(8, value.length, true);
    view.setUint32(12, 1, true);
  } else {
    if (value.length > 65535) throw new Error('test fixture exceeds a KBC2 size');
    view.setUint16(6, value.length, true);
    view.setUint32(8, 1, true);
  }
  view.setUint16(dims, stride, true);
  view.setUint16(dims + 2, 6, true);
  view.setUint16(dims + 4, 1200, true);
  if (stride > 48) {
    for (let i = 0; i < 6 + 1200; i++) value.fill(0xa5, offset + stride * i + 48, offset + stride * (i + 1));
  }
  const last = offset + stride * (6 + 1199);
  value[last] = 1025 & 255; value[last + 1] = 1025 >> 8;
  value[last + 2] = 88;
  const c = crc16(value.subarray(0, value.length - 2));
  value[value.length - 2] = c & 255; value[value.length - 1] = c >> 8;
  return value;
}
const fullEntries = [
  { space: 0, key: 'init', kind: 3, value: new Uint8Array([1]) },
  { space: 1, key: 'cbA', kind: 7, value: collectionRecord() },
  { space: 1, key: 'cbB', kind: 7, value: collectionRecord() },
  { space: 1, key: 'active', kind: 3, value: new Uint8Array([1]) },
];
{
  const full = v2Backup(fullEntries);
  const checked = verifyBackup(backupText(full));
  ck(checked.version === 2 && checked.blob.length > 115000 && checked.fields === 4,
     'v2 verifies both full 1200-slot records, beyond the old transfer and u16 total limits');
  ck(Buffer.from(checked.blob).equals(Buffer.from(full)), 'every upper-slot byte survives verification');
  const flipped = full.slice();
  flipped[flipped.length - 80] ^= 0xff;
  const validText = backupText(full);
  const corruptText = validText.replace(/IMPORT [0-9a-f]/, 'IMPORT z');
  ckThrows(() => verifyBackup(corruptText), 'unrecognised', 'corrupted hex in a large backup is rejected');
  ckThrows(() => verifyBackup(backupText(flipped, false)),
           'checksum', 'large backup checksum corruption is rejected');
  const wrongNamespace = fullEntries.map((e) => ({ ...e }));
  wrongNamespace[1].space = 2;
  ckThrows(() => verifyBackup(backupText(v2Backup(wrongNamespace))), 'namespace',
           'an unknown namespace cannot silently drop collection data');
  ckThrows(() => verifyBackup(backupText(v2Backup(fullEntries.slice(0, 1)))), 'collection',
           'a v2 save missing its authoritative collection is refused');
  const duplicate = [...fullEntries, fullEntries[3]];
  ckThrows(() => verifyBackup(backupText(v2Backup(duplicate))), 'duplicate',
           'duplicate namespaced fields are refused');
  const wrongCount = full.slice();
  wrongCount[5]++;
  ckThrows(() => verifyBackup(backupText(wrongCount)), 'count', 'a recomputed checksum cannot hide a missing field');
  const brokenRecords = fullEntries.map((e) => ({ ...e, value: e.value.slice() }));
  brokenRecords[1].value[brokenRecords[1].value.length - 80] ^= 0xff;
  brokenRecords[2].value[brokenRecords[2].value.length - 80] ^= 0xff;
  ckThrows(() => verifyBackup(backupText(v2Backup(brokenRecords))), 'checkpoint',
           'a valid outer checksum cannot hide two corrupt collection checkpoints');
}

{
  const futureEntries = fullEntries.map((e) => ({
    ...e, value: ['cbA', 'cbB'].includes(e.key) ? collectionRecord({ wide: true, stride: 56 }) : e.value,
  }));
  const full = backupBlob(futureEntries);
  const checked = verifyBackup(backupText(full));
  ck(checked.version === 3 && checked.fields === 4 &&
     futureEntries.filter((e) => e.kind === 7).every((e) => e.value.length > 65535),
     'v3 verifies individual KBC3 collection snapshots above 65535 bytes');
  ck(Buffer.from(checked.blob).equals(Buffer.from(full)),
     'future-stride records and their unknown suffix bytes remain intact');
  ck(verifyBackup(backupText(backupBlob(fullEntries))).version === 3,
     'v3 also accepts historical KBC2 snapshots');
  const narrowKbc3 = fullEntries.map((e) => ({
    ...e, value: ['cbA', 'cbB'].includes(e.key) ? collectionRecord({ wide: true }) : e.value,
  }));
  ck(verifyBackup(backupText(v2Backup(narrowKbc3))).version === 2,
     'a v2 transfer retains its u16 fields even when it contains a fitting KBC3 snapshot');

  const extended = futureEntries.map((e) => ({
    ...e, value: e.kind === 7 ? collectionRecord({ wide: true, stride: 56, headerSize: 24, tail: 9 }) : e.value,
  }));
  for (const e of extended.filter((e) => e.kind === 7)) {
    e.value[4] = 77; // checkpoint versions are diagnostic, not layout discriminators
    const c = crc16(e.value.subarray(0, e.value.length - 2));
    new DataView(e.value.buffer).setUint16(e.value.length - 2, c, true);
  }
  ck(verifyBackup(backupText(backupBlob(extended))).fields === 4,
     'larger KBC3 headers, appended data and diagnostic versions stay forward-readable');

  const futureDex = futureEntries.map((e) => ({ ...e, value: e.value.slice() }));
  for (const e of futureDex.filter((e) => e.kind === 7)) {
    const view = new DataView(e.value.buffer);
    view.setInt16(22, 0x7fff, true);
    view.setUint16(e.value.length - 2, crc16(e.value.subarray(0, e.value.length - 2)), true);
  }
  ck(verifyBackup(backupText(backupBlob(futureDex))).fields === 4,
     'browser format verification preserves future positive dex numbers instead of pinning today\'s count');

  const lengthAt = 8 + 2 + futureEntries[0].key.length + 1;
  const impossible = full.slice();
  new DataView(impossible.buffer).setUint32(lengthAt, 0xffffffff, true);
  ckThrows(() => verifyBackup(backupText(impossible)), 'length',
     'an unsigned u32 length cannot wrap or run past the transfer body');
  const shortHeader = new Uint8Array(lengthAt + 3 + 2);
  shortHeader.set(full.subarray(0, lengthAt + 3));
  ckThrows(() => verifyBackup(backupText(shortHeader)), 'length',
     'a truncated four-byte field length is rejected before upload');
  const tooLargeField = futureEntries.map((e) => ({
    ...e, value: e.key === 'cbA' ? new Uint8Array(96 * 1024 + 1) : e.value,
  }));
  ckThrows(() => verifyBackup(backupText(backupBlob(tooLargeField))), 'length',
     'a field above the shared 96 KiB collection ceiling is refused');
  const tooLarge = new Uint8Array(SAVE_TRANSFER_MAX + 1);
  tooLarge.set(full.subarray(0, 8));
  ckThrows(() => verifyBackup(backupText(tooLarge)), 'transfer limit',
     'the 262144-byte transfer ceiling is enforced before decoding the whole text');

  const mutations = [
    ['u32 checkpoint size', (v) => v.setUint32(8, v.byteLength & 0xffff, true)],
    ['checkpoint header size', (v) => v.setUint16(6, 15, true)],
    ['empty party dimension', (v) => v.setUint16(18, 0, true)],
    ['oversized box dimension', (v) => v.setUint16(20, 1201, true)],
    ['record stride beyond the body', (v) => v.setUint16(16, 65535, true)],
    ['negative stored dex prefix', (v) => v.setInt16(22, -1, true)],
  ];
  for (const [what, mutate] of mutations) {
    const invalid = futureEntries.map((e) => ({ ...e, value: e.value.slice() }));
    for (const e of invalid.filter((e) => e.kind === 7)) {
      mutate(new DataView(e.value.buffer));
      const c = crc16(e.value.subarray(0, e.value.length - 2));
      new DataView(e.value.buffer).setUint16(e.value.length - 2, c, true);
    }
    ckThrows(() => verifyBackup(backupText(backupBlob(invalid))), 'checkpoint',
       `checksummed ${what} is rejected before replacement`);
  }
  const oneCorrupt = futureEntries.map((e) => ({ ...e, value: e.value.slice() }));
  oneCorrupt[1].value[oneCorrupt[1].value.length - 80] ^= 0xff;
  ck(verifyBackup(backupText(backupBlob(oneCorrupt))).fields === 4,
     'one valid collection checkpoint still permits recovery from its damaged twin');

  const unknown = { space: 0, key: 'future', kind: 1, value: new Uint8Array([42]) };
  ck(verifyBackup(backupText(backupBlob([unknown], 1))).fields === 1,
     'v1 retains its original permissive game-key semantics');
  for (const version of [2, 3]) {
    ckThrows(() => verifyBackup(backupText(backupBlob([...fullEntries, unknown], version))), 'unknown',
       `v${version} retains strict namespaced game-key validation`);
  }
}

// ---- truncation: the case that used to pass
// A capture that stops early still ends with a plausible commit line. Dropping a
// middle chunk and keeping the commit is exactly what a short serial read looks
// like, and nothing caught it before verifyBackup compared the declared length.
{
  const lines = golden.trim().split('\n');
  const cut = [...lines.slice(0, lines.length - 3), 'IMPORT'];
  ckThrows(() => verifyBackup(cut.join('\n')), 'declares',
           'a truncated capture is refused by the declared byte count');
  // and with the header line removed, the checksum has to be what catches it
  const noHeader = cut.filter((l) => !l.startsWith('#'));
  ckThrows(() => verifyBackup(noHeader.join('\n')), 'checksum',
           'and with no header line the checksum catches it instead');
}

// ---- a single corrupted byte, in the BODY
// Deliberately not the first data line: byte 0 is the 'T' of the magic, so
// flipping it is refused for that reason instead and the checksum never runs.
// The first version of this test did exactly that and proved nothing about the
// checksum at all.
{
  const lines = golden.trim().split('\n');
  const dataIdx = lines.map((l, i) => [l, i])
    .filter(([l]) => l.startsWith('IMPORT ') && l.length > 40).map(([, i]) => i);
  const i = dataIdx[Math.floor(dataIdx.length / 2)];
  const hex = lines[i].slice(7);
  const flipped = ((Number.parseInt(hex.slice(0, 2), 16) ^ 0xff) & 0xff).toString(16).padStart(2, '0');
  lines[i] = `IMPORT ${flipped}${hex.slice(2)}`;
  ckThrows(() => verifyBackup(lines.join('\n')), 'checksum',
           'one flipped byte in the body fails the checksum');
}

// ---- every byte position, so no region of the blob is unprotected
{
  const lines = golden.trim().split('\n');
  const dataIdx = lines.map((l, i) => [l, i]).filter(([l]) => l.startsWith('IMPORT ')).map(([, i]) => i);
  let missed = 0;
  for (const i of dataIdx) {
    const hex = lines[i].slice(7);
    for (let b = 0; b < hex.length; b += 2) {
      const copy = [...lines];
      const byte = Number.parseInt(hex.slice(b, b + 2), 16);
      copy[i] = `IMPORT ${hex.slice(0, b)}${((byte ^ 0xff) & 0xff).toString(16).padStart(2, '0')}${hex.slice(b + 2)}`;
      try { verifyBackup(copy.join('\n')); missed++; } catch { /* refused, as it must be */ }
    }
  }
  ck(missed === 0, `no single-byte corruption anywhere in the blob is accepted`);
}

// ---- format rules
ckThrows(() => verifyBackup('IMPORT 544B5053\nIMPORT\nIMPORT DEAD\nIMPORT'),
         'after the final', 'content after the commit line is refused');
ckThrows(() => verifyBackup('IMPORT 544B5053010000'), 'commit',
         'a backup with no commit line is refused');
ckThrows(() => verifyBackup('IMPORT'), 'no data', 'a commit line on its own is refused');
ckThrows(() => verifyBackup('IMPORT 544B5053ABC\nIMPORT'), 'odd',
         'an odd-length data line is refused');
// Long enough to reach the magic check -- a 4-byte blob is refused for being too
// short, which is a different guard and would have made this assertion a lie.
ckThrows(() => verifyBackup(`IMPORT ${'aa'.repeat(12)}\nIMPORT`), 'not a TamaPoke save',
         'a blob with the wrong magic is refused');
ckThrows(() => verifyBackup('IMPORT 544B5053\nIMPORT'), 'too short',
         'and one too short to hold a header is refused for being short');
ckThrows(() => verifyBackup('hello\nIMPORT'), 'unrecognised',
         'a line that is not a command at all is refused');

// a version this page does not understand, with the checksum made correct so the
// version check is genuinely what refuses it
{
  const blob = new Uint8Array(12);
  blob.set([0x54, 0x4b, 0x50, 0x53], 0);
  blob[4] = 99;
  const c = crc16(blob.subarray(0, blob.length - SAVE_CRC_BYTES));
  blob[blob.length - 2] = c & 0xff;
  blob[blob.length - 1] = c >> 8;
  const hex = [...blob].map((b) => b.toString(16).padStart(2, '0')).join('');
  ckThrows(() => verifyBackup(`IMPORT ${hex}\nIMPORT`), 'version',
           'a future save format is refused by version, not by checksum');
}

// ---- comments and whitespace are tolerated, since a player may paste anything
{
  const messy = `# TamaPoke save, ${parsed.blob.length} bytes. Paste this whole block back.\n\n`
    + parsed.commands.slice(0, -1).map((c) => `  ${c}  `).join('\n\n')
    + '\n# a note the player added\nIMPORT\n';
  const ok = verifyBackup(messy);
  ck(ok.blob.length === parsed.blob.length,
     'blank lines, indentation and extra comments do not break a paste');
}

// ---- the board line the installer adds to what it downloads
{
  const lines = golden.trim().split('\n');
  const tagged = verifyBackup([lines[0], '# board 987d91858428', ...lines.slice(1)].join('\n'));
  ck(tagged.board === '987D91858428', 'a "# board" line is read back, so a restore can tell boards apart');
  ck(tagged.blob.length === parsed.blob.length && tagged.commands.length === parsed.commands.length,
     'and changes nothing that is sent to the board');
  ck(parsed.board === null, "the firmware's own EXPORT carries none");
}

// ---- the label, against web/dex.json, which gen_web_dex.py derives from the game
{
  const dex = JSON.parse(readFileSync(join(root, 'web/dex.json'), 'utf8'));
  const summary = saveSummary(described, dex);
  ck(summary.includes('CHARIZARD Lv.30'),
     `the fixture (EGG 6, LVL 30 on the firmware) is labelled with its species and level: "${summary}"`);
  ck(saveSummary(described, null).includes('dex 6'), 'and without the table it is still a number');
  ck(saveSummary({ dex: -1 }, dex) === 'an egg', 'an egg says so');
  ck(saveSummary({}, dex) === 'no details', 'and a blob with nothing readable does not throw');
  ck(backupFileName('A.S-H!', new Date(2026, 8, 27, 9, 5, 7)) === 'tamapoke-ASH-2026-09-27-09-05-07.tpsave',
     'backup files are named in local time, with only letters and digits of the trainer');
  ck(backupFileName('', new Date(2026, 0, 2, 3, 4, 5)).startsWith('tamapoke-save-2026-01-02'),
     'and a save with no trainer name still gets a name');
}

// ---- the upload protocol, driven against fake firmware
//
// This is the part that had no tests and broke: the restore path assumed every
// firmware with IMPORT also answers "IMPORT MORE". It does not -- v2.5, v3.3 and
// v3.4 accept a restore and never reply -- so the page hung 8 s per chunk and
// failed for exactly the people who most need to recover a save. sendBackup()
// takes its I/O as arguments so a board is not needed to prove it.
function fakeBoard({ acks = true, failAfter = -1, error = null, commitReply = 'IMPORT OK' }) {
  const io = { sent: [], paused: 0, logs: [], waits: [] };
  let chunk = 0;
  io.writeLine = async (line) => { io.sent.push(line); if (line !== 'IMPORT') chunk++; };
  io.pause = async () => { io.paused++; };
  io.log = (m) => io.logs.push(m);
  io.resetQueue = () => {};
  io.waitForAny = async (tokens, timeoutMs) => {
    io.waits.push(timeoutMs);
    if (tokens.includes('IMPORT OK')) return commitReply;
    if (error && chunk === failAfter) return error;
    if (!acks) return null;                          // silence: old firmware
    if (failAfter >= 0 && chunk > failAfter) return null;   // stopped replying
    return 'IMPORT MORE';
  };
  return io;
}

const commands = parsed.commands;
const chunks = commands.length - 1;

// Runs a case that MUST succeed, reporting a throw as a failure rather than
// letting it abort the run. Without this the negative check for the regression
// below killed the process with a stack trace and skipped every later test --
// a crash says less than an assertion, and it hides whatever came after it.
async function ckResolves(fn, what) {
  try {
    return await fn();
  } catch (error) {
    console.log(`FAIL  ${what}`);
    console.log(`      threw: ${error.message}`);
    bad++;
    return null;
  }
}

{
  const io = fakeBoard({ acks: true });
  const out = await ckResolves(() => sendBackup(commands, io),
                              'modern firmware: the upload completes');
  ck(out?.acknowledged === true, 'modern firmware: every chunk is acknowledged');
  ck(io.paused === 0, 'and no blind delay is used at all');
  ck(io.sent.length === commands.length, `all ${chunks} chunks plus the commit were sent`);
  ck(io.sent.at(-1) === 'IMPORT', 'ending with the commit line');
  ck(io.waits.at(-1) >= 60000, 'commit allows a minute for durable staging and verified replay');
}

{
  const wideEntries = fullEntries.map((e) => ({
    ...e, value: e.kind === 7 ? collectionRecord({ wide: true, stride: 56 }) : e.value,
  }));
  const full = verifyBackup(backupText(backupBlob(wideEntries)));
  const io = fakeBoard({ acks: true });
  const result = await ckResolves(() => sendBackup(full.commands, io),
    'a full 1200-slot backup uploads without truncation');
  ck(result?.acknowledged && io.sent.length === full.commands.length && io.sent.length > 1200,
     'every v3 chunk of both individually wide collection copies reaches the board before commit');
}

{
  // v2.5 / v3.3 / v3.4: accepts the data, never replies.
  const io = fakeBoard({ acks: false });
  const out = await ckResolves(() => sendBackup(commands, io),
                              'pre-v3.15 firmware: the restore still completes');
  ck(out?.acknowledged === false, 'pre-v3.15 firmware: the missing acknowledgement is detected');
  ck(io.sent.length === commands.length,
     'and every chunk plus the commit was still sent, which is the regression this covers');
  ck(io.paused === chunks, `every chunk is paced instead (${io.paused} pauses)`);
  ck(io.logs.some((m) => m.includes('pre-v3.15')), 'and the player is told why');
}

{
  // Silence AFTER the first chunk is a board that stopped, not old firmware.
  const io = fakeBoard({ acks: true, failAfter: 2 });
  let message = null;
  try { await sendBackup(commands, io); } catch (e) { message = e.message; }
  ck(message !== null && message.includes('stopped acknowledging'),
     'a board that goes quiet mid-upload is a failure, not a fallback');
}

{
  // A named error is a real error whichever firmware it is.
  const io = fakeBoard({ acks: true, failAfter: 1, error: 'IMPORT BAD' });
  let message = null;
  try { await sendBackup(commands, io); } catch (e) { message = e.message; }
  ck(message === 'IMPORT BAD', 'a named error is reported as itself');
}

{
  const io = fakeBoard({ acks: true, commitReply: 'IMPORT REJECTED' });
  let message = null;
  try { await sendBackup(commands, io); } catch (e) { message = e.message; }
  ck(message === 'IMPORT REJECTED', 'a refused commit is reported');
}

{
  let message = null;
  try { await sendBackup(['IMPORT AA'], fakeBoard({})); } catch (e) { message = e.message; }
  ck(message !== null && message.includes('verifyBackup'),
     'a command list that is not from verifyBackup() is refused');
}

console.log(bad ? `\n${bad} FAILURE(S)` : '\nall good');
process.exit(bad ? 1 : 0);
