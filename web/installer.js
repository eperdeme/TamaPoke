const byId = (id) => document.getElementById(id);
const enc = new TextEncoder();
const MB = (bytes) => `${(bytes / 1048576).toFixed(1)} MB`;
const titleCase = (value) => value.charAt(0).toUpperCase() + value.slice(1);
const pause = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

let editions = [];
let packs = {};
let port = null;
let reader = null;
let writer = null;
let readCarry = '';
let readQueue = [];
let readWaiters = [];
let packProtocol = false;
let sdAvailable = true;
let busy = false;
const installedPacks = new Map();

const logElement = byId('log');
function log(message) {
  const stamp = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  logElement.textContent += `${logElement.textContent ? '\n' : ''}[${stamp}] ${message}`;
  logElement.scrollTop = logElement.scrollHeight;
}

function refreshIcons() {
  if (window.lucide) window.lucide.createIcons();
}

async function loadJson(path) {
  const response = await fetch(path, { cache: 'no-cache' });
  if (!response.ok) throw new Error(`${path}: HTTP ${response.status}`);
  return response.json();
}

async function loadEditions() {
  try {
    const catalogue = await loadJson('editions.json');
    editions = catalogue.editions || [];
  } catch (error) {
    editions = [{
      id: 'standard', name: 'Standard', channel: 'Stable', manifest: 'manifest.json',
      description: 'All game systems and languages, with region artwork on microSD.', recommended: true,
    }];
    log(`Firmware catalogue fallback: ${error.message}`);
  }

  const versions = await Promise.all(editions.map(async (edition) => {
    try {
      const manifest = await loadJson(edition.manifest);
      return manifest.version || 'unknown';
    } catch (_error) {
      return 'unavailable';
    }
  }));

  const host = byId('editions');
  host.textContent = '';
  host.classList.toggle('single', editions.length === 1);
  editions.forEach((edition, index) => {
    const option = document.createElement('div');
    option.className = `edition-option${edition.recommended || index === 0 ? ' selected' : ''}`;
    option.innerHTML = `
      <label>
        <input type="radio" name="edition" value="${edition.id}" ${edition.recommended || index === 0 ? 'checked' : ''}>
        <span class="choice-control" aria-hidden="true"></span>
        <span class="option-copy">
          <span class="option-title"><span>${edition.name}</span><span class="tag">${edition.channel || 'Build'}</span></span>
          <p>${edition.description}</p>
          <span class="pack-version">v${versions[index]}</span>
        </span>
      </label>`;
    option.querySelector('input').addEventListener('change', () => selectEdition(edition.id));
    host.append(option);
  });
  const selected = editions.find((edition) => edition.recommended) || editions[0];
  if (selected) selectEdition(selected.id);
}

function selectEdition(id) {
  const edition = editions.find((item) => item.id === id);
  if (!edition) return;
  for (const option of document.querySelectorAll('.edition-option')) {
    option.classList.toggle('selected', option.querySelector('input').value === id);
  }
  byId('flash-button').setAttribute('manifest', edition.manifest);
  byId('edition-summary').textContent = edition.description;
}

async function loadPacks() {
  try {
    const catalogue = await loadJson('paks.json');
    packs = catalogue.regions || {};
    renderPacks();
    const count = Object.keys(packs).length;
    const bytes = Object.values(packs).reduce((total, pack) => total + pack.bytes, 0);
    byId('catalogue-summary').textContent = `${count} published region${count === 1 ? '' : 's'}, ${MB(bytes)} total.`;
  } catch (error) {
    byId('regions').innerHTML = '<p class="muted">The region catalogue could not be loaded.</p>';
    byId('catalogue-summary').textContent = error.message;
    log(`Pack catalogue failed: ${error.message}`);
  }
}

function sortedPacks() {
  return Object.entries(packs).sort((left, right) => left[1].index - right[1].index);
}

function renderPacks() {
  const host = byId('regions');
  host.textContent = '';
  for (const [region, meta] of sortedPacks()) {
    const option = document.createElement('div');
    option.className = 'pack-option';
    option.dataset.region = region;
    option.innerHTML = `
      <label>
        <input type="checkbox" data-region="${region}" disabled>
        <span class="choice-control" aria-hidden="true"></span>
        <span class="option-copy">
          <span class="option-title"><span>${titleCase(region)}</span><span class="pack-state">Not checked</span></span>
          <p>${meta.sprites} files / ${MB(meta.bytes)}</p>
          <span class="pack-version">web ${meta.crc32}</span>
        </span>
      </label>`;
    const input = option.querySelector('input');
    input.addEventListener('change', () => {
      option.classList.toggle('selected', input.checked);
      refreshSelection();
    });
    host.append(option);
  }
  syncControls();
}

function emitLine(line) {
  const clean = line.replace(/\r$/, '').trim();
  const waiter = readWaiters.shift();
  if (waiter) {
    clearTimeout(waiter.timer);
    waiter.resolve(clean);
  } else {
    readQueue.push(clean);
  }
}

async function pumpSerial(activeReader) {
  const decoder = new TextDecoder();
  try {
    while (reader === activeReader) {
      const { value, done } = await activeReader.read();
      if (done) break;
      readCarry += decoder.decode(value, { stream: true });
      let newline;
      while ((newline = readCarry.indexOf('\n')) >= 0) {
        emitLine(readCarry.slice(0, newline));
        readCarry = readCarry.slice(newline + 1);
      }
    }
  } catch (error) {
    if (writer) log(`Serial connection closed: ${error.message}`);
  } finally {
    if (reader === activeReader) setConnected(false, 'Board disconnected');
  }
}

function readLine(timeoutMs = 6000) {
  if (readQueue.length) return Promise.resolve(readQueue.shift());
  return new Promise((resolve) => {
    const waiter = { resolve, timer: null };
    waiter.timer = setTimeout(() => {
      const index = readWaiters.indexOf(waiter);
      if (index >= 0) readWaiters.splice(index, 1);
      resolve(null);
    }, timeoutMs);
    readWaiters.push(waiter);
  });
}

async function writeLine(line) {
  if (!writer) throw new Error('Board is not connected.');
  await writer.write(enc.encode(`${line}\n`));
}

async function waitForAny(tokens, timeoutMs = 6000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const line = await readLine(deadline - Date.now());
    if (line === null) return null;
    if (tokens.includes(line)) return line;
  }
  return null;
}

async function commandLines(command, timeoutMs = 6000) {
  readQueue = [];
  await writeLine(command);
  const lines = [];
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const line = await readLine(deadline - Date.now());
    if (line === null) return { outcome: null, lines };
    if (line === 'DONE' || line === 'ERR') return { outcome: line, lines };
    lines.push(line);
  }
  return { outcome: null, lines };
}

async function expectDone(command, timeoutMs = 6000) {
  readQueue = [];
  await writeLine(command);
  return (await waitForAny(['DONE', 'ERR'], timeoutMs)) === 'DONE';
}

function setConnected(connected, message = connected ? 'Board connected' : 'Board not connected') {
  const state = byId('connection-state');
  state.classList.toggle('connected', connected);
  state.querySelector('span:last-child').textContent = message;
  if (!connected) {
    writer = null;
    reader = null;
  }
  syncControls();
}

function setBusy(value) {
  busy = value;
  syncControls();
}

function syncControls() {
  const connected = Boolean(writer);
  byId('connect').disabled = busy || connected || !('serial' in navigator);
  byId('refresh-packs').disabled = busy || !connected;
  byId('select-needed').disabled = busy || !connected || !Object.keys(packs).length;
  byId('backup').disabled = busy || !connected;
  byId('restore').disabled = busy || !connected;
  byId('restore-label').setAttribute('aria-disabled', String(busy || !connected));
  byId('files').disabled = busy || !connected;
  for (const input of document.querySelectorAll('[data-region]')) input.disabled = busy || !connected;
  refreshSelection();
}

function statusFor(meta) {
  if (!writer) return { text: 'Connect to inspect', kind: '', needed: false };
  if (!packProtocol) return { text: 'Version unknown', kind: 'update', needed: false };
  if (!sdAvailable) return { text: 'No SD card', kind: 'update', needed: false };
  const installed = installedPacks.get(meta.index) || 'missing';
  if (installed === meta.crc32.toLowerCase()) return { text: 'Current', kind: 'current', needed: false };
  if (installed === 'legacy') return { text: 'Installed / unversioned', kind: 'update', needed: false };
  if (/^[0-9a-f]{8}$/.test(installed)) return { text: 'Update available', kind: 'update', needed: true };
  return { text: 'Not installed', kind: 'missing', needed: true };
}

function updatePackStatuses() {
  for (const [region, meta] of sortedPacks()) {
    const option = document.querySelector(`.pack-option[data-region="${region}"]`);
    if (!option) continue;
    const status = statusFor(meta);
    const badge = option.querySelector('.pack-state');
    badge.textContent = status.text;
    badge.className = `pack-state ${status.kind}`.trim();
    option.dataset.current = String(status.kind === 'current');
    option.dataset.needed = String(status.needed);
    if (!status.needed) {
      const input = option.querySelector('input');
      input.checked = false;
      option.classList.remove('selected');
    }
  }
  refreshSelection();
}

function selectedPacks() {
  return [...document.querySelectorAll('[data-region]:checked')].map((input) => [input.dataset.region, packs[input.dataset.region]]);
}

function refreshSelection() {
  const selected = selectedPacks();
  const bytes = selected.reduce((total, entry) => total + entry[1].bytes, 0);
  byId('selection-title').textContent = selected.length
    ? `${selected.length} region${selected.length === 1 ? '' : 's'} selected`
    : 'No packs selected';
  byId('selection-detail').textContent = selected.length
    ? `${MB(bytes)}, roughly ${Math.max(1, Math.round(bytes / 1048576 / 3))} minutes over USB.`
    : (writer ? 'Current packs are left unselected.' : 'Connect a board to compare its SD card.');
  byId('install').disabled = busy || !writer || !selected.length;
}

async function queryInstalledPacks() {
  installedPacks.clear();
  sdAvailable = true;
  const result = await commandLines('PACKS', 2500);
  if (result.outcome === null) {
    packProtocol = false;
    log('This firmware cannot report pack versions. Flash the current edition to enable SD comparison.');
  } else {
    packProtocol = true;
    sdAvailable = result.outcome !== 'ERR';
    for (const line of result.lines) {
      const match = /^PACK (\d+) (missing|legacy|[0-9a-fA-F]{8})$/.exec(line);
      if (match) installedPacks.set(Number(match[1]), match[2].toLowerCase());
    }
    if (sdAvailable) {
      const current = sortedPacks().filter((entry) => installedPacks.get(entry[1].index) === entry[1].crc32.toLowerCase()).length;
      log(`SD inspected: ${current} of ${Object.keys(packs).length} published packs are current.`);
    } else {
      log('The board reports no mounted microSD card.');
    }
  }
  updatePackStatuses();
}

function parsePak(buffer) {
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

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let index = 0; index < 256; index++) {
    let value = index;
    for (let bit = 0; bit < 8; bit++) value = (value >>> 1) ^ (0xEDB88320 & -(value & 1));
    table[index] = value >>> 0;
  }
  return table;
})();

function crc32(bytes) {
  let crc = 0xFFFFFFFF;
  for (const byte of bytes) crc = CRC_TABLE[(crc ^ byte) & 0xFF] ^ (crc >>> 8);
  return ((crc ^ 0xFFFFFFFF) >>> 0).toString(16).padStart(8, '0');
}

function verifyPak(meta, buffer) {
  if (buffer.byteLength !== meta.bytes) throw new Error(`expected ${meta.bytes} bytes, received ${buffer.byteLength}`);
  const checksum = crc32(new Uint8Array(buffer));
  if (checksum !== meta.crc32.toLowerCase()) throw new Error(`checksum ${checksum} does not match ${meta.crc32}`);
}

async function sendOne(name, data) {
  await writeLine(`PUT ${name} ${data.length}`);
  if ((await waitForAny(['OK', 'ERR'])) !== 'OK') return false;
  for (let offset = 0; offset < data.length; offset += 2048) {
    await writer.write(data.slice(offset, offset + 2048));
    if ((await waitForAny(['#', 'ERR'])) !== '#') return false;
  }
  return (await waitForAny(['DONE', 'ERR'], 30000)) === 'DONE';
}

async function sendOneRetrying(name, data) {
  for (let attempt = 1; attempt <= 3; attempt++) {
    if (await sendOne(name, data)) return true;
    if (attempt < 3) {
      log(`Retrying ${name} (${attempt}/2)`);
      readQueue = [];
      await pause(250);
    }
  }
  return false;
}

function showProgress(label, done, total) {
  const percent = total ? Math.round(done / total * 100) : 0;
  byId('progress').hidden = false;
  byId('progress-label').textContent = label;
  byId('progress-value').textContent = `${percent}%`;
  byId('progress-fill').style.width = `${percent}%`;
}

async function sendAll(items, region) {
  for (let index = 0; index < items.length; index++) {
    const item = items[index];
    showProgress(`${titleCase(region)} / ${item.name.replace('mons/', '')}`, index, items.length);
    if (!await sendOneRetrying(item.name, item.data)) {
      log(`Failed after retries: ${item.name}`);
      return false;
    }
  }
  showProgress(`${titleCase(region)} complete`, items.length, items.length);
  return true;
}

async function loadRegion(region, meta) {
  log(`Downloading ${titleCase(region)} (${MB(meta.bytes)})...`);
  const response = await fetch(`sprites-${region}.pak`);
  if (!response.ok) throw new Error(`HTTP ${response.status} while downloading ${region}`);
  const buffer = await response.arrayBuffer();
  verifyPak(meta, buffer);
  const items = parsePak(buffer);
  log(`${titleCase(region)} verified (${meta.crc32}); ${items.length} files ready.`);

  if (packProtocol && !await expectDone(`PACK BEGIN ${meta.index}`)) {
    throw new Error(`board refused to begin the ${region} pack`);
  }
  if (!await sendAll(items, region)) return false;
  if (packProtocol) {
    if (!await expectDone(`PACK COMMIT ${meta.index} ${meta.crc32}`, 10000)) {
      throw new Error(`files arrived but the board could not validate the ${region} pack`);
    }
    installedPacks.set(meta.index, meta.crc32.toLowerCase());
  }
  log(`${titleCase(region)} is installed and verified.`);
  return true;
}

function downloadText(name, text) {
  const url = URL.createObjectURL(new Blob([text], { type: 'text/plain' }));
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = name;
  document.body.append(anchor);
  anchor.click();
  anchor.remove();
  URL.revokeObjectURL(url);
}

async function backupSave() {
  setBusy(true);
  try {
    readQueue = [];
    await writeLine('EXPORT');
    const lines = [];
    const deadline = Date.now() + 15000;
    while (Date.now() < deadline) {
      const line = await readLine(deadline - Date.now());
      if (line === null) throw new Error('the board did not finish the export');
      if (line === 'EXPORT FAIL') throw new Error('the firmware could not build a backup');
      if (line.startsWith('# TamaPoke save') || line.startsWith('IMPORT ')) lines.push(line);
      if (line === 'IMPORT') {
        lines.push(line);
        break;
      }
    }
    const chunks = lines.filter((line) => line.startsWith('IMPORT '));
    if (!chunks.length || lines.at(-1) !== 'IMPORT') throw new Error('the exported save was incomplete');
    const date = new Date().toISOString().slice(0, 10);
    downloadText(`tamapoke-save-${date}.tpsave`, `${lines.join('\n')}\n`);
    log(`Save backup downloaded (${chunks.length} data chunks).`);
  } catch (error) {
    log(`Backup failed: ${error.message}`);
  } finally {
    setBusy(false);
  }
}

function parseBackup(text) {
  const lines = text.split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
  const commands = [];
  for (const line of lines) {
    if (line.startsWith('#')) continue;
    if (line === 'IMPORT') {
      commands.push(line);
    } else if (/^IMPORT [0-9a-fA-F]+$/.test(line) && (line.length - 7) % 2 === 0) {
      commands.push(line);
    } else {
      throw new Error(`unrecognised backup line: ${line.slice(0, 32)}`);
    }
  }
  if (commands.length < 2 || commands.at(-1) !== 'IMPORT' || commands.slice(0, -1).some((line) => line === 'IMPORT')) {
    throw new Error('backup is missing its data or final commit line');
  }
  const magic = commands[0].slice(7, 15).toUpperCase();
  if (magic !== '544B5053') throw new Error('this is not a TamaPoke save');
  return commands;
}

async function restoreSave(file) {
  let commands;
  try {
    commands = parseBackup(await file.text());
  } catch (error) {
    log(`Restore refused before upload: ${error.message}`);
    return;
  }
  if (!window.confirm(`Replace the save on the connected board with ${file.name}? The board will restart after validation.`)) return;

  setBusy(true);
  try {
    readQueue = [];
    for (const command of commands.slice(0, -1)) {
      await writeLine(command);
      if (packProtocol) {
        const reply = await waitForAny(['IMPORT MORE', 'IMPORT ODD', 'IMPORT BAD', 'IMPORT FULL'], 8000);
        if (reply !== 'IMPORT MORE') throw new Error(reply || 'the board stopped acknowledging save data');
      } else {
        await pause(125);
      }
    }
    await writeLine('IMPORT');
    const result = await waitForAny(['IMPORT OK', 'IMPORT REJECTED', 'IMPORT EMPTY'], 15000);
    if (result !== 'IMPORT OK') throw new Error(result || 'the board did not answer');
    log('Save validated and restored. The board is restarting.');
    setConnected(false, 'Board restarting');
  } catch (error) {
    log(`Restore failed: ${error.message}`);
  } finally {
    setBusy(false);
  }
}

byId('connect').addEventListener('click', async () => {
  try {
    setBusy(true);
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });
    reader = port.readable.getReader();
    writer = port.writable.getWriter();
    readCarry = '';
    readQueue = [];
    void pumpSerial(reader);
    setConnected(true);
    log('Board connected. Reading the microSD pack catalogue...');
    await queryInstalledPacks();
  } catch (error) {
    log(`Could not connect: ${error.message}`);
    setConnected(false);
  } finally {
    setBusy(false);
  }
});

byId('refresh-packs').addEventListener('click', async () => {
  setBusy(true);
  try { await queryInstalledPacks(); }
  catch (error) { log(`Pack refresh failed: ${error.message}`); }
  finally { setBusy(false); }
});

byId('select-needed').addEventListener('click', () => {
  for (const option of document.querySelectorAll('.pack-option')) {
    const input = option.querySelector('input');
    input.checked = option.dataset.needed === 'true';
    option.classList.toggle('selected', input.checked);
  }
  refreshSelection();
});

byId('install').addEventListener('click', async () => {
  const selected = selectedPacks();
  if (!selected.length || !writer) return;
  setBusy(true);
  let completed = 0;
  try {
    for (const [region, meta] of selected) {
      if (!await loadRegion(region, meta)) break;
      completed++;
    }
    log(completed === selected.length
      ? `All ${completed} selected regions are ready. Restart the board to reload thumbnails.`
      : `Stopped after ${completed} of ${selected.length} regions.`);
  } catch (error) {
    log(`Install stopped: ${error.message}`);
  } finally {
    updatePackStatuses();
    setBusy(false);
  }
});

byId('files').addEventListener('change', async (event) => {
  const files = [...event.target.files];
  if (!files.length) return;
  setBusy(true);
  try {
    if (packProtocol) {
      for (const [, meta] of sortedPacks()) await expectDone(`PACK BEGIN ${meta.index}`);
    }
    const items = [];
    for (const file of files) items.push({ name: `mons/${file.name}`, data: new Uint8Array(await file.arrayBuffer()) });
    const ok = await sendAll(items, 'custom files');
    log(ok ? `${items.length} custom files installed. Pack versions are now marked unverified.` : 'Custom file transfer failed.');
    await queryInstalledPacks();
  } catch (error) {
    log(`Custom file transfer failed: ${error.message}`);
  } finally {
    event.target.value = '';
    setBusy(false);
  }
});

byId('backup').addEventListener('click', backupSave);
byId('restore').addEventListener('change', (event) => {
  const [file] = event.target.files;
  if (file) void restoreSave(file);
  event.target.value = '';
});
byId('clear-log').addEventListener('click', () => { logElement.textContent = ''; });

if (!('serial' in navigator)) byId('unsupported').hidden = false;
if ('serial' in navigator) {
  navigator.serial.addEventListener('disconnect', () => setConnected(false, 'Board disconnected'));
}

await Promise.all([loadEditions(), loadPacks()]);
syncControls();
refreshIcons();