import {
  verifyBackup, describeBackup, sendBackup, saveSummary, backupFileName,
} from './savefile.js?v=46404cdccf162854';
import { parsePak, verifyPak, packIsCurrent, probeSum, installFile } from './packs.js?v=2d4d719f12e1ccb6';

const byId = (id) => document.getElementById(id);
const enc = new TextEncoder();
const MB = (bytes) => `${(bytes / 1048576).toFixed(1)} MB`;
const titleCase = (value) => value.charAt(0).toUpperCase() + value.slice(1);
const pause = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const sentence = (text) => `${String(text).replace(/[.\s]+$/, '')}.`;
const PACK_QUERY_TIMEOUT_MS = 20000;
const PACK_COMMIT_TIMEOUT_MS = 30000;
// Web Serial defaults to a 255-byte buffer, which splits every 2 KB block into
// nine hand-offs between the page and the browser.
const SERIAL_OPTIONS = { baudRate: 115200, bufferSize: 16384 };
// Measured on a board with a 128 GB card: v3.25's transfer mode, and v3.24 before it.
const USB_BYTES_PER_SEC = { current: 112 * 1024, older: 41 * 1024 };
// How long a board that has just restarted may take to reach its console.
const BOOT_PROBE_MS = 10000;

let editions = [];
let selectedEditionId = '';
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
// A label, and only a label. It comes from the board's efuse MAC so that two
// devices' backups can be told apart in the history list. It is NOT a secret and
// NOT a key: nothing is authorised by it, which is exactly why the history stays
// in this browser instead of on a server somewhere keyed by it.
let boardId = '';
// fw= from STATS; empty on firmware that predates reporting it (v3.25 and older).
// Both describe the LAST board this page talked to, and outlive the connection.
let boardFw = '';
let sumSupported = null;       // SUM, probed once per connection
let measuredRate = 0;          // bytes/s the last install managed, for the next estimate
let dexInfo = null;            // web/dex.json: species names and the level rule
let reconnect = null;          // { board, reason, until } while a board is expected back
let reconnecting = false;
let failedRegions = [];        // what Retry sends
let downgradeOverride = false;
const backedUp = new Set();    // boards with a verified backup from this page session
const installedPacks = new Map();
const REQUIRED_FLASH_PARTS = new Map([
  [0, 'firmware/bootloader.bin'],
  [0x8000, 'firmware/partitions.bin'],
  [0xe000, 'firmware/boot_app0.bin'],
  [0x10000, 'firmware/app.bin'],
]);
const verifiedPacks = new Map();

const logElement = byId('log');
function log(message) {
  const stamp = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  logElement.textContent += `${logElement.textContent ? '\n' : ''}[${stamp}] ${message}`;
  logElement.scrollTop = logElement.scrollHeight;
}

// A result goes next to the button that asked for it. The activity log is at the
// bottom of the page, which is not where anyone is looking when a transfer stops.
function setStatus(area, kind, message, action = null) {
  const host = byId(`${area}-status`);
  host.hidden = false;
  host.className = `status status-${kind}`;
  host.textContent = '';
  const text = document.createElement('span');
  text.textContent = message;
  host.append(text);
  if (action) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'button button-small button-secondary';
    button.textContent = action.label;
    button.disabled = busy;
    button.addEventListener('click', () => action.run());
    host.append(button);
  }
}

function clearStatus(area) {
  const host = byId(`${area}-status`);
  host.hidden = true;
  host.textContent = '';
}

function report(area, what, error) {
  log(`${what}: ${error.message}`);
  setStatus(area, 'error', sentence(`${what}: ${error.message}`));
}

function refreshIcons() {
  if (window.lucide) window.lucide.createIcons();
}

async function loadJson(path, options = {}) {
  const response = await fetch(path, { cache: 'no-cache', ...options });
  if (!response.ok) throw new Error(`${path}: HTTP ${response.status}`);
  return response.json();
}

const normalizeVersion = (value) => String(value || '').trim().replace(/^v/i, '');
const isVersion = (value) => /^\d+(\.\d+)*$/.test(value);

// By component, so 3.10 is newer than 3.9.
function compareVersions(left, right) {
  const a = normalizeVersion(left).split('.').map(Number);
  const b = normalizeVersion(right).split('.').map(Number);
  for (let i = 0; i < Math.max(a.length, b.length); i++) {
    const difference = (a[i] || 0) - (b[i] || 0);
    if (difference) return difference;
  }
  return 0;
}

function validateReleaseManifest(manifest, tag) {
  if (normalizeVersion(manifest.version) !== normalizeVersion(tag)) return false;
  if (manifest.new_install_prompt_erase !== true) return false;
  const build = (manifest.builds || []).find((item) => item.chipFamily === 'ESP32-S3');
  if (!build || !Array.isArray(build.parts) || build.parts.length !== REQUIRED_FLASH_PARTS.size) return false;
  return build.parts.every((part) => {
    const expectedPath = REQUIRED_FLASH_PARTS.get(Number(part.offset));
    const path = typeof part.path === 'string' ? part.path.split('?', 1)[0] : '';
    return path === expectedPath;
  });
}

function releaseManifestUrl(repository, tag) {
  const [owner, name] = repository.split('/');
  return `https://raw.githubusercontent.com/${encodeURIComponent(owner)}/${encodeURIComponent(name)}/${encodeURIComponent(tag)}/web/manifest.json`;
}

async function loadReleaseVersions(config) {
  if (!config.repository) return [];
  const [owner, name] = config.repository.split('/');
  if (!owner || !name) throw new Error('editions.json repository must be owner/name');
  const count = Math.max(1, Math.min(Number(config.maxReleases) || 8, 20));
  const api = `https://api.github.com/repos/${encodeURIComponent(owner)}/${encodeURIComponent(name)}/releases?per_page=${count}`;
  const releases = await loadJson(api, { headers: { Accept: 'application/vnd.github+json' } });
  const checked = await Promise.all(releases.filter((release) => !release.draft).map(async (release) => {
    const manifestUrl = releaseManifestUrl(config.repository, release.tag_name);
    try {
      const manifest = await loadJson(manifestUrl);
      if (!validateReleaseManifest(manifest, release.tag_name)) {
        log(`Skipped release ${release.tag_name}: its tag and safe flash manifest do not agree.`);
        return null;
      }
      const date = release.published_at ? new Date(release.published_at) : null;
      return {
        id: `release-${release.id}`,
        name: release.name || `TamaPoke ${release.tag_name}`,
        channel: release.prerelease ? 'Preview' : 'Release',
        manifest: manifestUrl,
        version: normalizeVersion(manifest.version),
        description: date && !Number.isNaN(date.valueOf())
          ? `Published ${date.toLocaleDateString(undefined, { year: 'numeric', month: 'short', day: 'numeric' })}.`
          : 'Published on GitHub Releases.',
        notes: release.body && release.body.trim() ? release.body.trim() : 'No changelog was provided for this release.',
        releaseUrl: release.html_url,
        prerelease: release.prerelease,
      };
    } catch (error) {
      log(`Skipped release ${release.tag_name}: ${error.message}`);
      return null;
    }
  }));
  return checked.filter(Boolean);
}

function appendEditionCard(host, edition, selected) {
  const option = document.createElement('div');
  option.className = `edition-option${selected ? ' selected' : ''}`;
  const label = document.createElement('label');
  const input = document.createElement('input');
  input.type = 'radio';
  input.name = 'edition';
  input.value = edition.id;
  input.checked = selected;
  input.addEventListener('change', () => selectEdition(edition.id));

  const control = document.createElement('span');
  control.className = 'choice-control';
  control.setAttribute('aria-hidden', 'true');
  const copy = document.createElement('span');
  copy.className = 'option-copy';
  const title = document.createElement('span');
  title.className = 'option-title';
  const name = document.createElement('span');
  name.textContent = edition.name;
  const channel = document.createElement('span');
  channel.className = 'tag';
  channel.textContent = edition.channel || 'Build';
  const description = document.createElement('p');
  description.textContent = edition.description;
  const version = document.createElement('span');
  version.className = 'pack-version';
  version.textContent = `v${edition.version}`;
  title.append(name, channel);
  copy.append(title, description, version);
  label.append(input, control, copy);
  option.append(label);
  host.append(option);
}

async function loadEditions() {
  let config;
  try {
    config = await loadJson('editions.json');
  } catch (error) {
    config = { current: {} };
    log(`Firmware catalogue fallback: ${error.message}`);
  }

  const currentConfig = config.current || {};
  const currentManifestPath = currentConfig.manifest || 'manifest.json';
  let currentManifest = { version: 'unknown' };
  try { currentManifest = await loadJson(currentManifestPath); }
  catch (error) { log(`Current firmware manifest unavailable: ${error.message}`); }
  const current = {
    id: 'current',
    name: currentConfig.name || 'Current Pages build',
    channel: currentConfig.channel || 'Unreleased',
    manifest: currentManifestPath,
    version: normalizeVersion(currentManifest.version) || 'unknown',
    description: currentConfig.description || 'The build currently hosted by this GitHub Pages site.',
    notes: currentConfig.notes || 'This build has not been published as an immutable GitHub Release yet.',
    releaseUrl: `https://github.com/${config.repository || 'eperdeme/TamaPoke'}/releases`,
  };

  let released = [];
  let releasesReached = true;
  try { released = await loadReleaseVersions(config); }
  catch (error) {
    releasesReached = false;
    log(`GitHub Releases unavailable; showing the current build only: ${error.message}`);
  }
  const hasCurrentRelease = released.some((edition) => edition.version === current.version);
  if (!hasCurrentRelease) {
    // The notes ship with the site, so they survive GitHub's API being down or
    // rate-limited (60 requests an hour per address) -- which is also exactly when
    // this entry used to call an already-released build "Unreleased".
    const notes = await loadReleaseNotes(current.version);
    if (notes) current.notes = notes;
    if (!releasesReached) {
      current.channel = 'Current';
      current.description = 'GitHub Releases could not be reached, so older versions are not listed.';
    }
  }
  editions = hasCurrentRelease ? released : [...released, current];
  const recommended = editions.find((edition) => !edition.prerelease) || editions[0] || current;

  const host = byId('editions');
  host.textContent = '';
  host.classList.toggle('single', editions.length === 1);
  for (const edition of editions) appendEditionCard(host, edition, edition.id === recommended.id);
  selectEdition(recommended.id);
}

// docs/release-notes/ is served beside web/, since Pages publishes the repo root.
async function loadReleaseNotes(version) {
  if (!isVersion(version)) return '';
  try {
    const response = await fetch(`../docs/release-notes/v${version}.md`, { cache: 'no-cache' });
    return response.ok ? (await response.text()).trim() : '';
  } catch {
    return '';
  }
}

function selectEdition(id) {
  const edition = editions.find((item) => item.id === id);
  if (!edition) return;
  selectedEditionId = id;
  for (const option of document.querySelectorAll('.edition-option')) {
    option.classList.toggle('selected', option.querySelector('input').value === id);
  }
  const flashButton = byId('flash-button');
  flashButton.manifest = edition.manifest;
  flashButton.setAttribute('manifest', edition.manifest);
  byId('edition-summary').textContent = edition.description;
  const versionLabel = `v${edition.version}`;
  byId('release-notes-title').textContent = edition.name.toLowerCase().includes(versionLabel.toLowerCase())
    ? edition.name
    : `${edition.name} - ${versionLabel}`;
  byId('release-notes-body').textContent = edition.notes;
  const releaseLink = byId('release-link');
  releaseLink.href = edition.releaseUrl;
  releaseLink.textContent = edition.id === 'current' ? 'All releases' : 'View on GitHub';
  refreshFlashAdvice();
}

function selectedEdition() {
  return editions.find((item) => item.id === selectedEditionId) || null;
}

// A downgrade waits for a backup of the board it would overwrite. A save written by
// newer firmware is exactly what older firmware may not read in full.
function downgradeBlocked() {
  const edition = selectedEdition();
  return Boolean(edition && boardId && isVersion(boardFw) && isVersion(edition.version)
    && compareVersions(edition.version, boardFw) < 0 && !backedUp.has(boardId) && !downgradeOverride);
}

// What the page knows about the last board it talked to, against the version picked.
function refreshFlashAdvice() {
  const edition = selectedEdition();
  if (!edition || !boardId || !isVersion(edition.version)) {
    clearStatus('advice');
  } else if (!isVersion(boardFw)) {
    setStatus('advice', 'info', `Board ${boardId} runs firmware that does not report its version `
      + `(v3.25 or older). v${edition.version} is selected.`);
  } else if (compareVersions(edition.version, boardFw) === 0) {
    setStatus('advice', 'info', `Board ${boardId} already runs v${boardFw}.`);
  } else if (compareVersions(edition.version, boardFw) > 0) {
    setStatus('advice', 'info', `Board ${boardId} runs v${boardFw}; v${edition.version} is an update.`);
  } else if (downgradeBlocked()) {
    setStatus('advice', 'warn', `v${edition.version} is OLDER than the v${boardFw} on board ${boardId}. `
      + 'Back up the save first: a save written by newer firmware may not load fully on older firmware.', {
      label: 'Install without a backup',
      run: () => {
        if (!window.confirm(`Install v${edition.version} over v${boardFw} without a backup? `
          + 'If the older firmware cannot read this save, nothing can bring it back.')) return;
        downgradeOverride = true;
        refreshFlashAdvice();
      },
    });
  } else {
    setStatus('advice', 'info', `v${edition.version} is a downgrade from v${boardFw}`
      + `${backedUp.has(boardId) ? '; this board was backed up a moment ago' : ''}.`);
  }
  syncControls();
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
    // releasePort(), not setConnected(false): that only forgets the reader and
    // writer, leaving the port open and locked so nothing could reopen it.
    if (reader === activeReader) void releasePort('Board disconnected');
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

// Hands the serial port back to the browser properly.
//
// setConnected(false) only forgets our references to the reader and writer. That
// is enough for the UI, and it was all this page ever needed while installing was
// something you did in a separate dialog with the connection closed by hand. It
// is NOT enough to let go of the port: the locks stay held and the port stays
// open, so esp-web-tools cannot claim it and the flash fails with the device
// already in use. Anything that wants to hand off has to come through here.
//
// Every step is best-effort on purpose. A board that has just restarted (which is
// exactly what a restore does) throws from cancel() and close(), and failing to
// tidy up must not become the error the player sees.
async function releasePort(message = 'Board not connected') {
  const hadReader = reader;
  const hadWriter = writer;
  const hadPort = port;
  reader = null;                      // stops pumpSerial's loop, which watches this
  writer = null;
  port = null;
  readCarry = '';
  readQueue = [];
  // Anything waiting on the board hears now that it is gone, not at its timeout.
  const waiting = readWaiters;
  readWaiters = [];
  for (const waiter of waiting) {
    clearTimeout(waiter.timer);
    waiter.resolve(null);
  }
  packProtocol = false;
  sumSupported = null;
  try { await hadReader?.cancel(); } catch { /* already gone */ }
  try { hadReader?.releaseLock(); } catch { /* already released */ }
  try { hadWriter?.releaseLock(); } catch { /* already released */ }
  try { await hadPort?.close(); } catch { /* already closed */ }
  setConnected(false, message);
}

// Opens a port the user picked, or one getPorts() says this origin already has.
async function attach(selected) {
  await selected.open(SERIAL_OPTIONS);
  port = selected;
  reader = port.readable.getReader();
  writer = port.writable.getWriter();
  readCarry = '';
  readQueue = [];
  void pumpSerial(reader);
  setConnected(true);
}

let wakeLock = null;
let wakeLockPending = false;
// An install of every region takes the best part of an hour, and a computer that
// goes to sleep ends it. Held only while busy; hiding the tab releases it, so
// coming back takes it again.
async function syncWakeLock() {
  if (busy && !wakeLock && !wakeLockPending && 'wakeLock' in navigator
      && document.visibilityState === 'visible') {
    wakeLockPending = true;
    try {
      const lock = await navigator.wakeLock.request('screen');
      wakeLock = lock;
      lock.addEventListener('release', () => { if (wakeLock === lock) wakeLock = null; });
    } catch {
      // refused or unsupported: a convenience, never a requirement
    } finally {
      wakeLockPending = false;
    }
    if (!busy) void syncWakeLock();
  } else if (!busy && wakeLock) {
    const lock = wakeLock;
    wakeLock = null;
    try { await lock.release(); } catch { /* already released */ }
  }
}

function setBusy(value) {
  busy = value;
  syncControls();
  void syncWakeLock();
}

// EVERY control that talks to the board is gated here. The list once missed Back up
// save, which could then write EXPORT into the middle of a pack transfer and close
// the port that transfer was using.
function syncControls() {
  const connected = Boolean(writer);
  const serial = 'serial' in navigator;
  byId('connect').disabled = busy || connected || !serial;
  byId('backup-save').disabled = busy || !serial;
  // Disabled rather than intercepted: a disabled button never reaches esp-web-tools.
  // Looking for a board that restarted is not a reason to stop somebody flashing.
  byId('flash-activate').disabled = (busy && !reconnecting) || downgradeBlocked();
  byId('refresh-packs').disabled = busy || !connected;
  byId('select-needed').disabled = busy || !connected || !Object.keys(packs).length;
  byId('backup').disabled = busy || !connected;
  byId('restore-button').disabled = busy || !connected;
  byId('files-button').disabled = busy || !connected;
  for (const input of document.querySelectorAll('[data-region]')) input.disabled = busy || !connected;
  for (const button of document.querySelectorAll('.status button')) button.disabled = busy;
  refreshSelection();
}

function statusFor(meta) {
  if (!writer) return { text: 'Connect to inspect', kind: '', needed: false };
  if (!packProtocol) return { text: 'Version unknown', kind: 'update', needed: false };
  if (!sdAvailable) return { text: 'No SD card', kind: 'update', needed: false };
  const installed = installedPacks.get(meta.index) || 'missing';
  if (packIsCurrent(meta, installed)) return { text: 'Current', kind: 'current', needed: false };
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
    ? `${MB(bytes)}, ${transferEstimate(bytes)} over USB.`
    : (writer ? 'Current packs are left unselected.' : 'Connect a board to compare its SD card.');
  byId('install').disabled = busy || !writer || !selected.length;
  // The history list is rebuilt only when it changes, so its Restore buttons
  // have to be re-gated here or they would keep whatever state they were built
  // with -- a live button with no board behind it, which is the greyed-but-still-
  // tappable fault uiButtonDisabled() exists to prevent on the device itself.
  for (const button of document.querySelectorAll('.history-restore')) {
    button.disabled = busy || !writer;
  }
}

function formatDuration(seconds) {
  if (seconds < 60) return 'under a minute';
  const minutes = Math.round(seconds / 60);
  if (minutes < 90) return `${minutes} min`;
  return `${Math.floor(minutes / 60)} h ${minutes % 60} min`;
}

// The rate this board last managed, once there is one. Before that, v3.25's --
// or a range, since firmware that does not report its version may be the slow kind.
function transferEstimate(bytes) {
  if (measuredRate) return `about ${formatDuration(bytes / measuredRate)}`;
  const fast = `about ${formatDuration(bytes / USB_BYTES_PER_SEC.current)}`;
  if (boardFw) return fast;
  return `${fast} (up to ${formatDuration(bytes / USB_BYTES_PER_SEC.older)} on firmware before v3.25)`;
}

// STATS names the board and, after v3.25, its firmware version. False when nothing
// answered at all: a board in download mode, or one not running TamaPoke. Firmware
// too old to print board= still answers, and a backup with no label is still a backup.
async function queryStats(timeoutMs) {
  const result = await commandLines('STATS', timeoutMs);
  if (result.outcome === null) return false;
  boardId = '';
  boardFw = '';
  for (const line of result.lines) {
    const board = /^board=([0-9A-Fa-f]{6,16})$/.exec(line);
    if (board) boardId = board[1].toUpperCase();
    const fw = /^fw=(\d+(?:\.\d+)*)$/.exec(line);
    if (fw) boardFw = fw[1];
  }
  return true;
}

function boardLabel() {
  if (!boardId) return 'Board connected';
  return `Board ${boardId}${boardFw ? ` \u00b7 v${boardFw}` : ''}`;
}

// Who the board is, then what is on its card. A board that says nothing is let go
// at once, so the firmware installer can have the port.
async function inspectBoard(probeMs = 3000, attempts = 2) {
  let answered = false;
  for (let attempt = 0; attempt < attempts && !answered && writer; attempt++) {
    answered = await queryStats(probeMs);
  }
  if (!answered) {
    await releasePort('No TamaPoke firmware answered');
    log('No TamaPoke firmware answered STATS, so the port was released for the firmware installer.');
    setStatus('pack', 'warn', 'No TamaPoke firmware answered. If this board is new, or was started in '
      + 'download mode, install the firmware in step 01 first.');
    return false;
  }
  await describeBoard();
  return true;
}

async function describeBoard() {
  setConnected(true, boardLabel());
  log(`Connected: ${boardLabel()}.`);
  refreshFlashAdvice();
  await renderHistory();
  await queryInstalledPacks();
}

async function queryInstalledPacks(mountTried = false) {
  installedPacks.clear();
  sdAvailable = true;
  const result = await commandLines('PACKS', PACK_QUERY_TIMEOUT_MS);
  if (result.outcome === null) {
    packProtocol = false;
    log('The board did not answer PACKS within 20 seconds.');
    setStatus('pack', 'warn', 'This board did not report its packs: its firmware predates pack versions '
      + '(v3.16), or it was busy. Install the current firmware in step 01, or press Refresh.');
  } else {
    packProtocol = true;
    sdAvailable = result.outcome !== 'ERR';
    for (const line of result.lines) {
      const match = /^PACK (\d+) (missing|legacy|[0-9a-fA-F]{8})$/.exec(line);
      if (match) installedPacks.set(Number(match[1]), match[2].toLowerCase());
    }
    for (const [, meta] of sortedPacks()) {
      const installed = installedPacks.get(meta.index);
      if (packIsCurrent(meta, installed)) verifiedPacks.set(meta.index, installed);
    }
    if (sdAvailable) {
      const current = sortedPacks().filter((entry) => packIsCurrent(entry[1], installedPacks.get(entry[1].index))).length;
      log(`SD inspected: ${current} of ${Object.keys(packs).length} published packs are current.`);
    } else {
      log('The board reports no mounted microSD card.');
      if (!mountTried && await mountCard()) return queryInstalledPacks(true);
    }
  }
  updatePackStatuses();
}

// PACKS says ERR when no card is mounted. SD MOUNT tries again without touching the
// card, which also finds one inserted after boot; only when that fails is formatting
// offered, behind a click and a confirmation. Older firmware formatted any card it
// could not read, unasked.
async function mountCard() {
  const { outcome } = await commandLines('SD MOUNT', 10000);
  if (outcome === 'DONE') {
    log('The board mounted the microSD card.');
    return true;
  }
  if (outcome === null) {
    setStatus('pack', 'warn', 'The board has no microSD card it can use. Insert one, then restart the board.');
  } else {
    setStatus('pack', 'warn', 'The board cannot read its microSD card. It may be missing, unformatted, or '
      + 'exFAT, as most cards over 32 GB are sold. Formatting it erases everything on it.',
    { label: 'Format card', run: () => void formatCard() });
  }
  return false;
}

async function formatCard() {
  if (!writer || !window.confirm('Format the microSD card in the board? EVERYTHING on it is erased. '
    + 'The board only formats a card it cannot read.')) return;
  setBusy(true);
  clearStatus('pack');
  try {
    log('Formatting the microSD card; a large card can take a few minutes...');
    const { outcome } = await commandLines('SD MOUNT FORMAT', 300000);
    if (outcome === null) {
      throw new Error('the board has not finished. A large card can take several minutes; press Refresh once it answers');
    }
    if (outcome !== 'DONE') {
      throw new Error('the board could not format the card. Format it as FAT32 on a computer, then press Refresh');
    }
    log('The card was formatted and mounted.');
    setStatus('pack', 'ok', 'The card is formatted and ready for packs.');
    await queryInstalledPacks(true);
  } catch (error) {
    report('pack', 'Format failed', error);
  } finally {
    setBusy(false);
  }
}

// The serial I/O that packs.js drives, with this page's port behind it.
const packIo = {
  writeLine,
  writeBytes: async (bytes) => {
    if (!writer) throw new Error('Board is not connected.');
    await writer.write(bytes);
  },
  waitForAny,
  command: commandLines,
  pause,
  resetQueue: () => { readQueue = []; },
  log,
};

function showProgress(label, fraction, detail = '') {
  const percent = Math.max(0, Math.min(100, Math.round(fraction * 100)));
  byId('progress').hidden = false;
  byId('progress-label').textContent = label;
  byId('progress-value').textContent = detail ? `${percent}% \u00b7 ${detail}` : `${percent}%`;
  byId('progress-fill').style.width = `${percent}%`;
  byId('progress-track').setAttribute('aria-valuenow', String(percent));
}

// The whole body with progress, and exactly `expected` bytes of it: a short read is
// reported as one, instead of surfacing later as a checksum mismatch.
async function fetchExact(url, expected, init, onProgress) {
  const response = await fetch(url, init);
  if (!response.ok) throw new Error(`HTTP ${response.status} for ${url}`);
  const out = new Uint8Array(expected);
  const body = response.body.getReader();
  let received = 0;
  for (;;) {
    const { value, done } = await body.read();
    if (done) break;
    if (received + value.length > expected) {
      await body.cancel().catch(() => {});
      throw new Error(`more than the expected ${expected} bytes arrived`);
    }
    out.set(value, received);
    received += value.length;
    onProgress(received);
  }
  if (received !== expected) throw new Error(`expected ${expected} bytes, received ${received}`);
  return out.buffer;
}

// Keyed by the pack's own CRC, as the firmware parts and scripts are by theirs:
// Pages lets a browser keep a file for 10 minutes, so right after a release a
// cached old pack failed its checksum. The one retry bypasses the cache outright.
async function downloadPak(region, meta, signal, onProgress) {
  const url = `sprites-${region}.pak?v=${meta.crc32}`;
  try {
    const buffer = await fetchExact(url, meta.bytes, { signal }, onProgress);
    verifyPak(meta, buffer);
    return buffer;
  } catch (error) {
    if (signal.aborted) throw error;
    log(`${titleCase(region)} did not download cleanly (${error.message}); trying again without the cache...`);
  }
  try {
    const buffer = await fetchExact(url, meta.bytes, { signal, cache: 'reload' }, onProgress);
    verifyPak(meta, buffer);
    return buffer;
  } catch (error) {
    if (signal.aborted) throw error;
    throw new Error(`the ${region} pack did not download cleanly (${error.message}). `
      + 'The site may be mid-update; try again in a few minutes');
  }
}

// Fetching starts here; the promise is awaited when the region's turn comes, so the
// next pack downloads while the one before it goes over USB.
function startDownload([region, meta], signal) {
  const job = { region, meta, received: 0, show: null };
  job.promise = downloadPak(region, meta, signal, (received) => {
    job.received = received;
    job.show?.();
  });
  job.promise.catch(() => {});   // reported when awaited, not as an unhandled rejection
  return job;
}

async function awaitDownload(job, where) {
  job.show = () => showProgress(`Downloading ${where}`, job.received / job.meta.bytes,
    `${MB(job.received)} of ${MB(job.meta.bytes)}`);
  job.show();
  try {
    return await job.promise;
  } finally {
    job.show = null;
  }
}

// By bytes and across every region in the install, with the time left taken from
// the rate this board is actually managing.
function batchDetail(batch) {
  const progress = batch.done + batch.file;
  const seconds = (batch.usbMs + (batch.since ? performance.now() - batch.since : 0)) / 1000;
  const detail = `${MB(progress)} of ${MB(batch.total)}`;
  if (seconds < 5 || progress < 262144) return `${detail} \u00b7 estimating time`;
  return `${detail} \u00b7 about ${formatDuration((batch.total - progress) / (progress / seconds))} left`;
}

async function sendRegion(region, meta, buffer, batch, where) {
  const items = parsePak(buffer);
  log(`${titleCase(region)} verified (${meta.crc32}); ${items.length} files ready.`);
  if (packProtocol && !await expectDone(`PACK BEGIN ${meta.index}`)) {
    throw new Error(`the board refused to begin the ${region} pack`);
  }
  let skipped = 0;
  batch.since = performance.now();
  try {
    for (const item of items) {
      const label = `${where} \u00b7 ${item.name.replace('mons/', '')}`;
      const started = performance.now();
      const result = await installFile(packIo, item, {
        sums: Boolean(sumSupported),
        onProgress: (bytes) => {
          batch.file = bytes;
          showProgress(label, (batch.done + bytes) / batch.total, batchDetail(batch));
        },
      });
      if (result === 'skipped') {
        skipped++;
      } else {
        batch.sent += item.data.length;
        batch.sentMs += performance.now() - started;
      }
      batch.done += item.data.length;
      batch.file = 0;
    }
  } finally {
    batch.usbMs += performance.now() - batch.since;
    batch.since = 0;
  }
  if (packProtocol) {
    if (!await expectDone(`PACK COMMIT ${meta.index} ${meta.crc32}`, PACK_COMMIT_TIMEOUT_MS)) {
      throw new Error(`files arrived but the board could not validate the ${region} pack`);
    }
    installedPacks.set(meta.index, meta.crc32.toLowerCase());
    verifiedPacks.set(meta.index, meta.crc32.toLowerCase());
  }
  const checked = sumSupported ? ', every file read back from the card' : '';
  const already = skipped ? `; ${skipped} of ${items.length} files were already there` : '';
  log(`${titleCase(region)} is installed${checked}${already}.`);
}

async function installRegions(selected) {
  if (!selected.length || !writer) return;
  setBusy(true);
  clearStatus('pack');
  failedRegions = [];
  const batch = {
    total: selected.reduce((sum, [, meta]) => sum + meta.bytes, 0),
    done: 0, file: 0, sent: 0, sentMs: 0, usbMs: 0, since: 0,
  };
  const aborter = new AbortController();
  let completed = 0;
  try {
    if (sumSupported === null) {
      sumSupported = await probeSum(packIo);
      log(sumSupported
        ? 'This firmware reads files back, so each one is checked on the card and any already there are skipped.'
        : 'This firmware cannot read files back (v3.25 and older), so files are checked only as they arrive.');
    }
    let job = startDownload(selected[0], aborter.signal);
    for (let i = 0; i < selected.length; i++) {
      const [region, meta] = selected[i];
      const where = `${titleCase(region)} (${i + 1} of ${selected.length})`;
      const buffer = await awaitDownload(job, where);
      job = selected[i + 1] ? startDownload(selected[i + 1], aborter.signal) : null;
      await sendRegion(region, meta, buffer, batch, where);
      completed++;
    }
    showProgress('Install complete', 1, MB(batch.total));
    if (batch.sent >= 4 * 1048576) measuredRate = batch.sent / (batch.sentMs / 1000);
    if (packProtocol) {
      await queryInstalledPacks();
      if (await restoreVerifiedPackMarkers()) await queryInstalledPacks();
    }
    // Firmware that reports fw= also reloads thumbs.bin by itself; older firmware
    // shows the new thumbnails only after a restart.
    const restart = boardFw ? '' : ' Restart the board so the gallery shows the new thumbnails.';
    log(`All ${completed} selected regions are ready.`);
    setStatus('pack', 'ok', `${completed} region${completed === 1 ? '' : 's'} installed`
      + `${sumSupported ? ' and read back from the card' : ''}.${restart}`);
  } catch (error) {
    failedRegions = selected.slice(completed);
    log(`Install stopped after ${completed} of ${selected.length} regions: ${error.message}`);
    if (writer && packProtocol) {
      try { await queryInstalledPacks(); } catch { /* the connection may have gone with the failure */ }
    }
    setStatus('pack', 'error', sentence(`Install stopped: ${error.message}`), { label: 'Retry', run: retryInstall });
  } finally {
    aborter.abort();
    updatePackStatuses();
    setBusy(false);
  }
}

function retryInstall() {
  if (!failedRegions.length) return;
  if (!writer) {
    setStatus('pack', 'warn', 'Connect the board again, then press Retry.', { label: 'Retry', run: retryInstall });
    return;
  }
  void installRegions(failedRegions);
}

async function restoreVerifiedPackMarkers() {
  const missing = [...verifiedPacks].filter(([index, crc]) => installedPacks.get(index) !== crc);
  if (!missing.length) return 0;
  log(`Restoring ${missing.length} verified pack marker${missing.length === 1 ? '' : 's'}...`);
  for (const [index, crc] of missing) {
    if (!await expectDone(`PACK BEGIN ${index}`) ||
        !await expectDone(`PACK COMMIT ${index} ${crc}`, PACK_COMMIT_TIMEOUT_MS)) {
      throw new Error(`could not restore the version marker for region ${index}`);
    }
    installedPacks.set(index, crc);
  }
  log(`Restored ${missing.length} verified pack marker${missing.length === 1 ? '' : 's'}.`);
  return missing.length;
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

// Reads the save off the board and VERIFIES it before handing it back.
//
// The verification is the point. The previous version accepted the capture if it
// had seen at least one chunk and a closing commit line, which a TRUNCATED read
// also satisfies -- so a short capture was downloaded and presented as a backup.
// verifyBackup() compares the byte count the board declared against the hex that
// actually arrived and checks the checksum, and it is the same function that
// vets a file on the way back in.
//
// Throws with a reason. A caller must never treat an unverified capture as a
// backup: that is the one failure mode a backup feature cannot recover from.
async function captureSave() {
  readQueue = [];
  await writeLine('EXPORT');
  const lines = [];
  const deadline = Date.now() + 20000;
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
  if (lines.at(-1) !== 'IMPORT') throw new Error('the export stopped before the end');
  // The board it came from, as a comment: a paste into the console ignores it, and
  // parseBackup() reads it, so a restore onto a different board can say so.
  if (boardId) lines.splice(1, 0, `# board ${boardId}`);
  const text = `${lines.join('\n')}\n`;
  const parsed = verifyBackup(text);           // throws if it is not sound
  return { text, parsed, describe: describeBackup(parsed.blob) };
}

async function backupSave() {
  setBusy(true);
  clearStatus('save');
  try {
    const capture = await captureSave();
    const kept = await storeBackup(capture);
    downloadCapture(capture);
    if (boardId) backedUp.add(boardId);
    log(`Save backup verified and downloaded (${capture.parsed.blob.length} bytes, `
        + `${capture.parsed.fields} fields).`);
    setStatus('save', 'ok', `Backup verified and downloaded: ${saveSummary(capture.describe, dexInfo)}.`
      + (kept ? '' : ' This browser would not keep a copy, so keep the file.'));
    refreshFlashAdvice();
  } catch (error) {
    report('save', 'Backup failed', error);
  } finally {
    setBusy(false);
  }
}

function downloadCapture(capture) {
  downloadText(backupFileName(capture.describe?.trainer), capture.text);
}

// ---------------------------------------------------------------------------
// Backup history, kept in this browser.
//
// IndexedDB rather than localStorage: it is asynchronous, it stores structured
// values without stringifying them, and it is not sharing a ~5 MB synchronous
// budget with everything else on the origin. A save is only a couple of KB so
// neither would run out, but there is no reason to pick the one that blocks.
//
// THIS IS A CONVENIENCE, NOT A BACKUP, and the page says so. It lives in one
// browser profile on one machine and it is deleted by "clear site data", by
// private browsing, and by moving to another computer. That is why every capture
// is also written to disk as a file: the file is the backup, and this is the
// thing that makes restoring a moment's work instead of a search through
// Downloads. Presenting it as durable would be worse than not having it.
const DB_NAME = 'tamapoke-saves';
const DB_STORE = 'backups';
const HISTORY_LIMIT = 12;

function openDb() {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, 1);
    request.onupgradeneeded = () => {
      const db = request.result;
      if (!db.objectStoreNames.contains(DB_STORE)) {
        db.createObjectStore(DB_STORE, { keyPath: 'id', autoIncrement: true });
      }
    };
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error || new Error('could not open the backup database'));
  });
}

function txDone(tx) {
  return new Promise((resolve, reject) => {
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error || new Error('the backup database rejected the write'));
    tx.onabort = () => reject(tx.error || new Error('the backup database aborted'));
  });
}

async function storeBackup(capture, note = '') {
  try {
    const db = await openDb();
    const record = {
      at: Date.now(),
      text: capture.text,
      bytes: capture.parsed.blob.length,
      board: boardId,
      trainer: capture.describe?.trainer || '',
      dex: capture.describe?.dex ?? null,
      ageMinutes: capture.describe?.ageMinutes ?? null,
      note,
    };
    const tx = db.transaction(DB_STORE, 'readwrite');
    tx.objectStore(DB_STORE).add(record);
    await txDone(tx);
    await pruneBackups(db);
    db.close();
    await renderHistory();
    return true;
  } catch (error) {
    // A verified capture is still a good backup even if the history could not be
    // written -- private browsing blocks IndexedDB entirely, and failing the whole
    // operation for that would be perverse.
    log(`This browser would not keep a copy of the save: ${error.message}`);
    return false;
  }
}

async function listBackups() {
  try {
    const db = await openDb();
    const rows = await new Promise((resolve, reject) => {
      const request = db.transaction(DB_STORE, 'readonly').objectStore(DB_STORE).getAll();
      request.onsuccess = () => resolve(request.result || []);
      request.onerror = () => reject(request.error);
    });
    db.close();
    return rows.sort((a, b) => b.at - a.at);
  } catch {
    return [];
  }
}

// The limit is per board, so a second board's backups cannot push the first's out.
async function pruneBackups(db) {
  const rows = await new Promise((resolve, reject) => {
    const request = db.transaction(DB_STORE, 'readonly').objectStore(DB_STORE).getAll();
    request.onsuccess = () => resolve(request.result || []);
    request.onerror = () => reject(request.error);
  });
  const kept = new Map();
  const doomed = [];
  for (const row of rows.sort((a, b) => b.at - a.at)) {
    const count = kept.get(row.board || '') || 0;
    if (count >= HISTORY_LIMIT) doomed.push(row);
    else kept.set(row.board || '', count + 1);
  }
  if (!doomed.length) return;
  const tx = db.transaction(DB_STORE, 'readwrite');
  for (const row of doomed) tx.objectStore(DB_STORE).delete(row.id);
  await txDone(tx);
}

async function clearBackups() {
  try {
    const db = await openDb();
    const tx = db.transaction(DB_STORE, 'readwrite');
    tx.objectStore(DB_STORE).clear();
    await txDone(tx);
    db.close();
  } catch (error) {
    log(`Could not clear the history: ${error.message}`);
  }
}

function historyLabel(row) {
  const when = new Date(row.at).toLocaleString(undefined, {
    year: 'numeric', month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit',
  });
  const bits = [saveSummary({ trainer: row.trainer, dex: row.dex, ageMinutes: row.ageMinutes }, dexInfo)];
  bits.push(`${row.bytes} bytes`);
  if (row.board) bits.push(`board ${row.board}`);
  if (row.note) bits.push(row.note);
  return { when, detail: bits.join(' \u00b7 ') };
}

async function renderHistory() {
  const host = byId('history');
  if (!host) return;
  const rows = await listBackups();
  host.textContent = '';
  byId('history-clear').disabled = !rows.length;
  if (!rows.length) {
    const empty = document.createElement('p');
    empty.className = 'muted';
    empty.textContent = 'No backups stored in this browser yet.';
    host.append(empty);
    return;
  }
  for (const row of rows) {
    const { when, detail } = historyLabel(row);
    const item = document.createElement('div');
    item.className = 'history-row';

    const copy = document.createElement('div');
    const title = document.createElement('strong');
    title.textContent = when;
    const sub = document.createElement('p');
    sub.className = 'muted';
    sub.textContent = detail;
    copy.append(title, sub);

    const actions = document.createElement('div');
    actions.className = 'history-actions';

    const restore = document.createElement('button');
    restore.className = 'button button-secondary history-restore';
    restore.textContent = 'Restore';
    restore.disabled = busy || !writer;
    restore.addEventListener('click', () => void restoreText(row.text, `the backup from ${when}`, row.board));

    const save = document.createElement('button');
    save.className = 'button button-secondary';
    save.textContent = 'Download';
    save.addEventListener('click', () => downloadText(backupFileName(row.trainer, new Date(row.at)), row.text));

    actions.append(restore, save);
    item.append(copy, actions);
    host.append(item);
  }
}

// This is deliberately separate from firmware installation. Both this page and
// esp-web-tools must call navigator.serial.requestPort(), and the browser requires
// a fresh user click for each picker. Once the verified backup is downloaded, the
// port is released so the independent Install firmware control can claim it.
async function backupBeforeInstall() {
  setBusy(true);
  clearStatus('flash');
  let captured = false;
  try {
    if (!writer) {
      log('Connecting to read the current save...');
      await attach(await navigator.serial.requestPort());
      if (!await queryStats(3000) && !await queryStats(3000)) {
        throw new Error('no TamaPoke firmware answered, so there is no save to back up. '
          + 'A new board can go straight to Install firmware');
      }
    }
    const capture = await captureSave();
    await storeBackup(capture);
    downloadCapture(capture);
    if (boardId) backedUp.add(boardId);
    captured = true;
    log(`Backup verified and downloaded (${capture.parsed.blob.length} bytes).`);
    setStatus('flash', 'ok', `Backup verified and downloaded: ${saveSummary(capture.describe, dexInfo)}. `
      + 'Now choose Install firmware.');
  } catch (error) {
    if (error.name === 'NotFoundError') log('No board was chosen, so no backup was taken.');
    else report('flash', 'No backup taken', error);
  } finally {
    await releasePort(captured ? 'Backup complete; board disconnected' : 'Board not connected');
    setBusy(false);
    refreshFlashAdvice();
  }
}

async function restoreText(text, label, fromBoard = '') {
  let incoming;
  try {
    incoming = verifyBackup(text);
  } catch (error) {
    report('save', 'Restore refused before upload', error);
    return;
  }
  setBusy(true);
  clearStatus('save');
  try {
    // The one action on this page that destroys a save, so it keeps one first: what
    // is on the board goes into Recent backups, and a wrong file is undone from there.
    // A save that will not export is why some people restore, so that is reported,
    // never fatal.
    let current = null;
    let unreadable = '';
    try { current = await captureSave(); } catch (error) { unreadable = error.message; }
    let kept = false;
    if (current) {
      kept = await storeBackup(current, 'kept before a restore');
      if (!kept) downloadCapture(current);
    }
    const source = incoming.board || fromBoard;
    const lines = [
      `Replace the save on ${boardId ? `board ${boardId}` : 'this board'} with ${label}?`,
      '',
      `On the board now: ${current ? saveSummary(current.describe, dexInfo) : `unreadable (${unreadable})`}`,
      `In the backup: ${saveSummary(describeBackup(incoming.blob), dexInfo)}`,
    ];
    if (source && boardId && source !== boardId) {
      lines.push('', `This backup was taken from board ${source}, not this one.`);
    }
    lines.push('', current
      ? (kept ? 'A copy of the current save is kept in Recent backups below.' : 'A copy of the current save was downloaded.')
      : 'The current save could not be read, so no copy of it exists.');
    lines.push('The board checks the backup, replaces its save and restarts.');
    if (!window.confirm(lines.join('\n'))) {
      log('Restore cancelled.');
      return;
    }
    readQueue = [];
    // The protocol lives in savefile.js so it can be tested without a board; this
    // only supplies the I/O. See sendBackup() for why the acknowledgement is
    // detected rather than assumed.
    await sendBackup(incoming.commands, {
      writeLine,
      waitForAny,
      pause,
      log,
      resetQueue: () => { readQueue = []; },
    });
    log('Save validated and restored. The board is restarting.');
    const expected = boardId;
    // A restart drops the USB device, so the port is gone whether we tidy up or
    // not -- releasing it properly is what lets the page open it again.
    await releasePort('Board restarting');
    setStatus('save', 'ok', 'Restored. The board is restarting, and this page reconnects when it is back.');
    expectBoard(expected, 'restore');
  } catch (error) {
    report('save', 'Restore failed', error);
  } finally {
    setBusy(false);
  }
}

// After a restore restarts the board, or the flasher closes, the board comes back by
// itself, and finding it again is this page's job rather than the player's.
// getPorts() lists ports this origin was granted before, and opening one needs no
// picker. After a restore the board is known and checked by its ID; after the
// flasher it is not, so only a lone candidate is taken.
let reconnectTimer = 0;
function scheduleReconnect(ms = 1500) {
  clearTimeout(reconnectTimer);
  reconnectTimer = setTimeout(() => void tryReconnect(), ms);
}

function expectBoard(board, reason) {
  reconnect = { board, reason, until: Date.now() + 30000 };
  scheduleReconnect(1000);
}

async function tryReconnect() {
  if (!reconnect || !('serial' in navigator)) return;
  if (port) { reconnect = null; return; }            // connected some other way meanwhile
  if (busy || reconnecting) { scheduleReconnect(); return; }
  if (Date.now() > reconnect.until) {
    reconnect = null;
    setStatus('pack', 'info', 'The board did not come back by itself. Press Connect board when it is ready.');
    return;
  }
  const wanted = reconnect;
  // Install firmware clears `reconnect`, and the port must then be left alone.
  const cancelled = () => reconnect !== wanted;
  reconnecting = true;
  setBusy(true);
  let found = false;
  try {
    const candidates = await navigator.serial.getPorts();
    if (!wanted.board && candidates.length > 1) {
      reconnect = null;
      setStatus('pack', 'info', 'More than one board is attached. Press Connect board to choose one.');
      return;
    }
    for (const candidate of candidates) {
      if (cancelled()) break;
      try { await attach(candidate); } catch { continue; }   // still being closed, or gone
      const answered = await queryStats(BOOT_PROBE_MS);
      if (cancelled()) {
        if (port) await releasePort();
        break;
      }
      if (answered && (!wanted.board || !boardId || boardId === wanted.board)) {
        found = true;
        break;
      }
      await releasePort('Board not connected');
    }
  } finally {
    reconnecting = false;
    setBusy(false);
  }
  if (found) {
    reconnect = null;
    log(`Found the board again after the ${wanted.reason}.`);
    setBusy(true);
    try { await describeBoard(); }
    catch (error) { report('pack', 'Could not read the board', error); }
    finally { setBusy(false); }
  } else if (reconnect) {
    scheduleReconnect();
  }
}

byId('connect').addEventListener('click', async () => {
  setBusy(true);
  clearStatus('pack');
  reconnect = null;
  try {
    await attach(await navigator.serial.requestPort());
    log('Board connected. Asking who it is...');
    await inspectBoard();
  } catch (error) {
    if (error.name === 'NotFoundError') log('No board was chosen.');
    else report('pack', 'Could not connect', error);
    if (port) await releasePort();
  } finally {
    setBusy(false);
  }
});

byId('refresh-packs').addEventListener('click', async () => {
  setBusy(true);
  clearStatus('pack');
  try { await queryInstalledPacks(); }
  catch (error) { report('pack', 'Pack refresh failed', error); }
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

byId('install').addEventListener('click', () => void installRegions(selectedPacks()));

byId('files-button').addEventListener('click', () => byId('files').click());
byId('files').addEventListener('change', async (event) => {
  const files = [...event.target.files];
  event.target.value = '';
  if (!files.length || !writer) return;
  setBusy(true);
  clearStatus('pack');
  try {
    if (packProtocol) {
      for (const [, meta] of sortedPacks()) await expectDone(`PACK BEGIN ${meta.index}`);
    }
    if (sumSupported === null) sumSupported = await probeSum(packIo);
    const items = [];
    for (const file of files) items.push({ name: `mons/${file.name}`, data: new Uint8Array(await file.arrayBuffer()) });
    const total = items.reduce((sum, item) => sum + item.data.length, 0);
    let done = 0;
    for (const item of items) {
      const label = `Custom files \u00b7 ${item.name.replace('mons/', '')}`;
      await installFile(packIo, item, {
        sums: Boolean(sumSupported),
        onProgress: (bytes) => showProgress(label, (done + bytes) / total, `${MB(done + bytes)} of ${MB(total)}`),
      });
      done += item.data.length;
    }
    showProgress('Custom files complete', 1);
    log(`${items.length} custom files installed. Pack versions are now marked unverified.`);
    setStatus('pack', 'ok', `${items.length} custom file${items.length === 1 ? '' : 's'} installed. `
      + 'Every pack now shows as unversioned until it is installed again.');
    await queryInstalledPacks();
  } catch (error) {
    report('pack', 'Custom file transfer failed', error);
  } finally {
    setBusy(false);
  }
});

byId('backup').addEventListener('click', backupSave);
byId('restore-button').addEventListener('click', () => byId('restore').click());
byId('restore').addEventListener('change', (event) => {
  const [file] = event.target.files;
  if (file) void file.text().then((text) => restoreText(text, file.name));
  event.target.value = '';
});
byId('backup-save').addEventListener('click', backupBeforeInstall);
// Install firmware hands the port to esp-web-tools, which cannot open one this page
// still holds. This runs first (it is on the button, theirs is on the slot around
// it), and the close is done long before anyone has picked a port in the chooser.
byId('flash-activate').addEventListener('click', () => {
  reconnect = null;
  if (port) void releasePort('Handed to the firmware installer');
});
byId('history-clear').addEventListener('click', async () => {
  if (!window.confirm('Delete every backup stored in this browser? The .tpsave files you downloaded are not affected.')) return;
  await clearBackups();
  await renderHistory();
  log('Browser backup history cleared.');
});
byId('clear-log').addEventListener('click', () => { logElement.textContent = ''; });

// Everything a bug report needs, in one paste. The board ID is a label, not a secret.
function diagnostics() {
  const edition = selectedEdition();
  const installed = sortedPacks().map(([region, meta]) => `${region}=${installedPacks.get(meta.index) || '?'}`);
  return [
    'TamaPoke installer diagnostics',
    `page: ${location.origin}${location.pathname} ${new URL(import.meta.url).search}`,
    `browser: ${navigator.userAgent}`,
    `web serial: ${'serial' in navigator ? 'yes' : 'no'}`,
    `board: ${boardId || 'unknown'}, firmware ${boardFw ? `v${boardFw}` : 'not reported'}, `
      + `${writer ? 'connected' : 'not connected'}`,
    `card: ${sdAvailable ? 'mounted' : 'not mounted'}, pack versions ${packProtocol ? 'yes' : 'no'}, `
      + `read-back ${sumSupported === null ? 'untested' : sumSupported ? 'yes' : 'no'}`,
    `packs: ${installed.join(' ')}`,
    `selected firmware: ${edition ? `${edition.name} v${edition.version}` : 'none'}`,
    '',
    '--- activity ---',
    logElement.textContent,
  ].join('\n');
}

byId('copy-log').addEventListener('click', async () => {
  const text = diagnostics();
  try {
    await navigator.clipboard.writeText(text);
    log('Diagnostics copied to the clipboard, ready to paste into a bug report.');
  } catch {
    downloadText('tamapoke-diagnostics.txt', text);
    log('The clipboard was not available, so the diagnostics were downloaded instead.');
  }
});

// Leaving mid-job loses no data -- pack markers and IMPORT's validation see to that
// -- but it does throw the job away, which can be most of an hour of transfer.
window.addEventListener('beforeunload', (event) => {
  if (!busy) return;
  event.preventDefault();
  event.returnValue = '';
});
document.addEventListener('visibilitychange', () => void syncWakeLock());

void renderHistory();   // the list is worth showing before anything is connected
void loadJson('dex.json').then((data) => { dexInfo = data; return renderHistory(); }).catch(() => {});

if (!('serial' in navigator)) byId('unsupported').hidden = false;
if ('serial' in navigator) {
  navigator.serial.addEventListener('disconnect', (event) => {
    // Every granted port fires this, so a second board being unplugged is not ours.
    if (event.target === port) void releasePort('Board disconnected');
  });
  navigator.serial.addEventListener('connect', () => { if (reconnect) scheduleReconnect(300); });
}
// esp-web-tools' dialog fires "closed" (it bubbles) as it lets go of the port, which
// is when the board it flashed can be found again.
document.addEventListener('closed', (event) => {
  if (event.target?.localName === 'ewt-install-dialog') expectBoard('', 'firmware install');
});

await Promise.all([loadEditions(), loadPacks()]);
syncControls();
refreshIcons();