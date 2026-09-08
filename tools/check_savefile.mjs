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
  SAVE_CRC_BYTES,
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

// ---- the upload protocol, driven against fake firmware
//
// This is the part that had no tests and broke: the restore path assumed every
// firmware with IMPORT also answers "IMPORT MORE". It does not -- v2.5, v3.3 and
// v3.4 accept a restore and never reply -- so the page hung 8 s per chunk and
// failed for exactly the people who most need to recover a save. sendBackup()
// takes its I/O as arguments so a board is not needed to prove it.
function fakeBoard({ acks = true, failAfter = -1, error = null, commitReply = 'IMPORT OK' }) {
  const io = { sent: [], paused: 0, logs: [] };
  let chunk = 0;
  io.writeLine = async (line) => { io.sent.push(line); if (line !== 'IMPORT') chunk++; };
  io.pause = async () => { io.paused++; };
  io.log = (m) => io.logs.push(m);
  io.resetQueue = () => {};
  io.waitForAny = async (tokens) => {
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
